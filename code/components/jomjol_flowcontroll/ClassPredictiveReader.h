#pragma once
#ifndef CLASSPREDICTIVEREADER_H
#define CLASSPREDICTIVEREADER_H

// ---------------------------------------------------------------------------------------------
// Predictive, physics-bounded digit reader
//
// Core idea: a utility meter can only advance as fast as physics allows. If we know the supply
// constraints (water pipe size + pressure, electrical service rating, gas pipe + delivery
// pressure) we can compute the maximum amount the displayed value could possibly have increased
// since the last confirmed reading. From that maximum increment we know:
//
//   * which low-order digits *might* have changed (and therefore must be read every round), and
//   * which high-order digits *cannot* have changed unless a carry propagates up from below.
//
// Combined with the confidence of the lower digits and a rolling history of past values, this lets
// the CNN skip inference on digits that provably cannot have moved, spend its effort on the fast
// least-significant digits, and only escalate to a more-significant digit when a carry is actually
// possible. It also yields a hard physical sanity bound that can reject impossible readings without
// the user having to hand-tune a MaxRate.
//
// This module is intentionally free of ESP-IDF / Arduino / project dependencies (only <ctime>,
// <cmath>, <vector>) so it compiles into the firmware *and* can be unit-tested on the host.
// ---------------------------------------------------------------------------------------------

#include <ctime>
#include <cmath>
#include <vector>
#include <cstddef>

namespace predictive {

// What kind of meter this sequence reads. Generic => no physics model (falls back to existing
// behaviour: every digit is read, only the user MaxRate / pixel-diff FastRead apply).
enum class Utility { Generic, Water, Electricity, Gas };

// Physical supply constraints for one sequence, plus the conversion from SI units to the meter's
// displayed value. Residential defaults are filled in so the feature "just works" until the user
// overrides them.
struct PhysicalLimits {
    Utility utility = Utility::Generic;

    // --- Water (residential supply defaults: 1" pipe, ~60 psi) ---
    double waterPipeDiameterMm = 25.4;    // 1" nominal (covers most households)
    double waterPressureKPa    = 410.0;   // ~60 psi
    double waterExpectedVelMs  = 3.0;     // practical erosion/noise velocity cap (for prediction)

    // --- Electricity (200 A / 240 V residential service) ---
    double elecServiceAmps  = 200.0;
    double elecServiceVolts = 240.0;

    // --- Gas (1.5" pipe, ~7" w.c. delivery, natural-gas density) ---
    double gasPipeDiameterMm = 38.1;      // 1.5" nominal (covers most households)
    double gasPressureKPa    = 1.74;      // ~7 inches water column
    double gasExpectedVelMs  = 20.0;      // practical residential branch velocity cap

    // How many SI units equal 1.0 of the displayed Value:
    //   Water       -> litres per displayed unit (m^3 display => 1000)
    //   Electricity -> kWh    per displayed unit (kWh display  => 1)
    //   Gas         -> m^3    per displayed unit (m^3 display   => 1)
    double unitsPerValue = 1000.0;

    // If >= 0, the user-set maximum rate (in displayed-value units per minute) overrides BOTH the
    // derived expected rate and the physical ceiling. Keeps the existing MaxRate escape hatch.
    double userMaxRatePerMin = -1.0;

    // True only when the user EXPLICITLY configured a supply model (a pipe diameter, or a service-amps
    // rating). The physical REJECTION ceiling (checkPlausibility) requires this, so that merely picking
    // a Meter Type (Utility = water/gas/electricity) does NOT start rejecting readings against the
    // residential DEFAULTS above - those defaults exist for PREDICTION only. Rejection is opt-in: with no
    // pipe/amps set, a water/gas/electric meter behaves like Generic for rejection (no ceiling). This is
    // why setting only "Meter Type = water" no longer flags normal rates as "exceeds physical max".
    bool supplyModelExplicit = false;

    // Multiplicative safety margin applied to the expected rate when deciding what *might* have
    // changed (so a slightly-faster-than-modelled round never silently drops a real digit change).
    double predictionSafetyFactor = 1.5;
};

// The two rates derived from PhysicalLimits, in displayed-value units per minute.
struct RateBounds {
    double expectedMaxPerMin = -1.0; // tighter, practical; used to PRIORITISE/gate digit reads
    double ceilingPerMin     = -1.0; // hard physical max; used to REJECT impossible readings
    bool   known             = false;// false => Generic / no model => no physics gating or rejection
};

// One observed reading.
struct Sample { double value; std::time_t t; };

// Fixed-capacity rolling history of recent readings (newest tracked). No heap churn.
class RollingHistory {
public:
    static const int CAP = 16;
    RollingHistory() : count_(0), head_(0) {}

    void add(double value, std::time_t t);
    int  size() const { return count_; }
    bool empty() const { return count_ == 0; }
    Sample latest() const;     // most recent (undefined if empty -> {0,0})
    Sample oldest() const;     // oldest retained

    // Observed average rate (value units per minute) across the retained window. Returns false if
    // there are fewer than two samples or no positive time span.
    bool observedRatePerMin(double& out) const;

    // Largest single-step rate seen in the window (value units per minute). Robustness diagnostic.
    bool peakRatePerMin(double& out) const;

    // Average spacing between samples (minutes) across the window - an estimate of the round cadence.
    // Returns false with fewer than two samples or no positive span.
    bool averageStepMinutes(double& out) const;

    void clear() { count_ = 0; head_ = 0; }

private:
    Sample buf_[CAP];
    int count_;
    int head_; // index of next write
    const Sample& at(int logicalIdxFromOldest) const;
};

// Per-digit input to the planner. One entry per integer/decimal digit position.
struct DigitState {
    int   place;       // 10^place is this digit's weight (0 = ones, -1 = tenths, 1 = tens, ...)
    int   value;       // last known digit value 0..9 (use 0 if unknown)
    float confidence;  // 0..1 confidence of the last read of this digit (1 = certain)
    bool  cacheValid;  // is there a usable cached value to fall back on if we skip?
};

// Per-digit decision produced by the planner.
struct DigitDecision {
    int   place;
    bool  mustRead;        // true => run the CNN; false => safe to reuse the cached value
    bool  carryWatch;      // true => this digit is the current carry frontier (worth watching)
    const char* reason;    // short human-readable justification (static string)
};

struct ReadPlan {
    RateBounds bounds;
    double maxDelta = 0.0;          // expected max value increment since last confirmed read
    double minutesElapsed = 0.0;
    int    highestReachablePlace = -9999; // most significant place expectedMaxDelta can directly reach
    bool   fullAudit = false;       // periodic whole-line re-read this round
    std::vector<DigitDecision> digits; // LSD..MSD, mirrors the input order
    int    plannedReads = 0;        // count of mustRead == true
};

// Per-digit rolling history of CONFIDENTLY-read class values (0..9 only). Inferred / resolved / "N"
// values are NEVER stored here - only what the CNN actually read with confidence. Used to resolve
// unknown digits and shown on the overview matrix.
class DigitHistory {
public:
    static const int CAP = 8;
    DigitHistory() : count_(0), head_(0) {}

    void addConfident(int v);                 // store a confident 0..9 read; out-of-range is ignored
    int  count() const { return count_; }
    bool empty() const { return count_ == 0; }
    bool mostRecent(int& out) const;          // newest stored value
    bool mode(int& out, int& votes) const;    // most frequent value + how many times it appears
    bool majority(int& out) const;            // a value held by strictly more than half the samples
    int  snapshot(int* dst, int maxn) const;  // copy up to maxn values newest-first; returns count
    void clear() { count_ = 0; head_ = 0; }

private:
    int buf_[CAP];
    int count_, head_;
    int at(int idxFromOldest) const;
};

// Resolve an "N"/unknown digit to a best-guess integer from its confident-read history and whether it
// can physically increment this round. Returns false (keep it "N") only when there is no confident
// history at all. Rules:
//   * cannot increment           -> use the history majority, else the most recent confident read.
//   * can increment, neighbour stable below -> assume unchanged -> most recent confident read.
//   * can increment, neighbour changed below -> genuinely uncertain -> best effort: most recent read.
bool resolveUnknownDigit(const DigitHistory& h, bool canIncrement, bool lowerNeighborChanged, int& out);

enum class Plausibility {
    Plausible,            // within the physical ceiling
    ExceedsPhysicalMax,   // jump is faster than physics allows -> almost certainly a misread
    NegativeChange,       // value decreased (caller decides if that's allowed)
    Unknown               // no physics model -> cannot judge
};

// --- Physics: each returns the SI rate the meter could advance at ---------------------------------
// Volumetric flow through a round pipe at a pressure-limited efflux velocity. Torricelli efflux
// velocity v = sqrt(2*P/rho) is the hard ceiling; a practical velocity cap (<=0 disables the cap)
// gives the tighter "expected" figure. Returns litres/minute.
double waterFlowLpm(double pipeDiameterMm, double pressureKPa, double velocityCapMs);

// Electrical energy rate = service apparent power. Returns kWh/minute. (This is already the hard
// ceiling: a service cannot deliver more than V*I.)
double electricEnergyKwhPerMin(double volts, double amps);

// Natural-gas volumetric flow through a pipe at delivery pressure. Returns m^3/minute.
double gasFlowM3PerMin(double pipeDiameterMm, double deliveryPressureKPa, double velocityCapMs);

// --- High-level API ------------------------------------------------------------------------------
// Derive the expected + ceiling rates (displayed-value units per minute) from the limits.
RateBounds deriveRateBounds(const PhysicalLimits& limits);

// Decide which digits must be read this round.
//  digitsLsdFirst : the meter's digits, least-significant first (the natural carry order).
//  minutesElapsed : time since the last confirmed reading.
//  periodicAudit  : caller's drift-backstop flag (force a full re-read this round).
//  confidenceFloor: a lower digit must be at least this confident before we trust it enough to
//                   skip the digit above it (default 0.90).
//  allowNegative  : if true (sequence flagged AllowNegativeRates), the value may move DOWN as fast as
//                   it could move up - the reachability/carry logic becomes symmetric so a falling
//                   value (e.g. a flow-rate display) gates its digits the same as a rising one.
ReadPlan planRead(const PhysicalLimits& limits,
                  const std::vector<DigitState>& digitsLsdFirst,
                  double minutesElapsed,
                  bool periodicAudit,
                  float confidenceFloor = 0.90f,
                  bool allowNegative = false);

// Hard physical sanity check on a completed reading. Uses the ceiling rate (never the tighter
// expected rate) so a physically-possible reading is never rejected. When allowNegative is true the
// bound is symmetric (|delta| <= ceiling*time): a decrease is allowed but only up to the same physical
// rate it could increase; when false, any decrease returns NegativeChange (the legacy behaviour).
Plausibility checkPlausibility(const PhysicalLimits& limits,
                               double previousValue,
                               double newValue,
                               double minutesElapsed,
                               bool allowNegative = false);

} // namespace predictive

#endif // CLASSPREDICTIVEREADER_H
