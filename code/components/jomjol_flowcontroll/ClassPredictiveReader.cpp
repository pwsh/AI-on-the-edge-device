#include "ClassPredictiveReader.h"

// ---------------------------------------------------------------------------------------------
// Implementation of the predictive, physics-bounded digit reader. See the header for the rationale.
// Dependency-free on purpose (host-testable).
// ---------------------------------------------------------------------------------------------

namespace predictive {

namespace {
    const double PI            = 3.14159265358979323846;
    const double WATER_DENSITY = 1000.0; // kg/m^3
    const double NATGAS_DENSITY = 0.78;  // kg/m^3 at residential delivery conditions (approx.)

    // Cross-sectional area of a round pipe, m^2, from a diameter in millimetres.
    double pipeAreaM2(double diameterMm) {
        if (diameterMm <= 0.0) return 0.0;
        const double r = (diameterMm / 1000.0) / 2.0;
        return PI * r * r;
    }

    // Pressure-limited efflux velocity (Torricelli), optionally clamped to a practical cap.
    // capMs <= 0 disables the cap (returns the pure physical ceiling velocity).
    double effluxVelocityMs(double pressureKPa, double density, double capMs) {
        if (pressureKPa <= 0.0 || density <= 0.0) return 0.0;
        const double v = std::sqrt(2.0 * (pressureKPa * 1000.0) / density);
        if (capMs > 0.0 && v > capMs) return capMs;
        return v;
    }
}

// --- Physics --------------------------------------------------------------------------------------

double waterFlowLpm(double pipeDiameterMm, double pressureKPa, double velocityCapMs) {
    const double a = pipeAreaM2(pipeDiameterMm);
    const double v = effluxVelocityMs(pressureKPa, WATER_DENSITY, velocityCapMs);
    const double m3s = a * v;            // m^3/s
    return m3s * 1000.0 * 60.0;          // -> litres/minute
}

double electricEnergyKwhPerMin(double volts, double amps) {
    if (volts <= 0.0 || amps <= 0.0) return 0.0;
    const double kw = (volts * amps) / 1000.0;
    return kw / 60.0;                    // kWh per minute
}

double gasFlowM3PerMin(double pipeDiameterMm, double deliveryPressureKPa, double velocityCapMs) {
    const double a = pipeAreaM2(pipeDiameterMm);
    const double v = effluxVelocityMs(deliveryPressureKPa, NATGAS_DENSITY, velocityCapMs);
    const double m3s = a * v;
    return m3s * 60.0;                   // -> m^3/minute
}

// --- Rate derivation ------------------------------------------------------------------------------

RateBounds deriveRateBounds(const PhysicalLimits& L) {
    RateBounds rb;

    // Explicit user override wins and is treated as both expected and ceiling.
    if (L.userMaxRatePerMin >= 0.0) {
        rb.expectedMaxPerMin = L.userMaxRatePerMin;
        rb.ceilingPerMin     = L.userMaxRatePerMin;
        rb.known             = true;
        return rb;
    }

    const double upv = (L.unitsPerValue > 0.0) ? L.unitsPerValue : 1.0;

    switch (L.utility) {
        case Utility::Water: {
            // No pipe diameter configured -> we can't derive a physical ceiling. Treat it as UNKNOWN
            // (don't reject) instead of deriving a 0-flow ceiling, which would otherwise flag EVERY
            // change as "exceeds physical max" (e.g. a rate-dial sequence left with no pipe set).
            if (L.waterPipeDiameterMm <= 0.0) { rb.known = false; break; }
            const double expectedSi = waterFlowLpm(L.waterPipeDiameterMm, L.waterPressureKPa, L.waterExpectedVelMs);
            const double ceilSi     = waterFlowLpm(L.waterPipeDiameterMm, L.waterPressureKPa, /*cap*/ 0.0);
            rb.expectedMaxPerMin = expectedSi / upv;
            rb.ceilingPerMin     = ceilSi / upv;
            rb.known = true;
        } break;
        case Utility::Electricity: {
            if (L.elecServiceAmps <= 0.0) { rb.known = false; break; }   // no service rating -> no ceiling
            // The service rating is already the hard ceiling; expected == ceiling.
            const double si = electricEnergyKwhPerMin(L.elecServiceVolts, L.elecServiceAmps);
            rb.expectedMaxPerMin = si / upv;
            rb.ceilingPerMin     = si / upv;
            rb.known = true;
        } break;
        case Utility::Gas: {
            if (L.gasPipeDiameterMm <= 0.0) { rb.known = false; break; }   // no pipe diameter -> no ceiling
            const double expectedSi = gasFlowM3PerMin(L.gasPipeDiameterMm, L.gasPressureKPa, L.gasExpectedVelMs);
            const double ceilSi     = gasFlowM3PerMin(L.gasPipeDiameterMm, L.gasPressureKPa, /*cap*/ 0.0);
            rb.expectedMaxPerMin = expectedSi / upv;
            rb.ceilingPerMin     = ceilSi / upv;
            rb.known = true;
        } break;
        case Utility::Generic:
        default:
            rb.known = false; // no physics model
            break;
    }
    return rb;
}

// --- Rolling history ------------------------------------------------------------------------------

void RollingHistory::add(double value, std::time_t t) {
    buf_[head_].value = value;
    buf_[head_].t = t;
    head_ = (head_ + 1) % CAP;
    if (count_ < CAP) count_++;
}

const Sample& RollingHistory::at(int logicalIdxFromOldest) const {
    // oldest retained sample is at (head_ - count_) mod CAP
    int start = (head_ - count_ + CAP) % CAP;
    int idx = (start + logicalIdxFromOldest) % CAP;
    return buf_[idx];
}

Sample RollingHistory::latest() const {
    if (count_ == 0) { Sample s{0.0, 0}; return s; }
    return at(count_ - 1);
}

Sample RollingHistory::oldest() const {
    if (count_ == 0) { Sample s{0.0, 0}; return s; }
    return at(0);
}

bool RollingHistory::observedRatePerMin(double& out) const {
    if (count_ < 2) return false;
    const Sample& a = at(0);
    const Sample& b = at(count_ - 1);
    const double mins = (double)(b.t - a.t) / 60.0;
    if (mins <= 0.0) return false;
    out = (b.value - a.value) / mins;
    return true;
}

bool RollingHistory::peakRatePerMin(double& out) const {
    if (count_ < 2) return false;
    bool any = false;
    double peak = 0.0;
    for (int i = 1; i < count_; ++i) {
        const Sample& a = at(i - 1);
        const Sample& b = at(i);
        const double mins = (double)(b.t - a.t) / 60.0;
        if (mins <= 0.0) continue;
        const double r = (b.value - a.value) / mins;
        if (!any || r > peak) { peak = r; any = true; }
    }
    if (!any) return false;
    out = peak;
    return true;
}

bool RollingHistory::averageStepMinutes(double& out) const {
    if (count_ < 2) return false;
    const double mins = (double)(at(count_ - 1).t - at(0).t) / 60.0;
    if (mins <= 0.0) return false;
    out = mins / (double)(count_ - 1);
    return true;
}

// --- The planner ----------------------------------------------------------------------------------

ReadPlan planRead(const PhysicalLimits& limits,
                  const std::vector<DigitState>& digits,
                  double minutesElapsed,
                  bool periodicAudit,
                  float confidenceFloor,
                  bool allowNegative) {
    ReadPlan plan;
    plan.bounds = deriveRateBounds(limits);
    plan.minutesElapsed = minutesElapsed < 0.0 ? 0.0 : minutesElapsed;
    plan.fullAudit = periodicAudit;

    const size_t n = digits.size();
    plan.digits.reserve(n);

    // No physics model, or a forced full audit, or no elapsed time reference: read everything.
    // (Falling back to the existing pixel-diff FastRead / MaxRate behaviour upstream.)
    const bool noModel = !plan.bounds.known || plan.bounds.expectedMaxPerMin < 0.0;
    if (noModel || periodicAudit || plan.minutesElapsed <= 0.0) {
        for (size_t i = 0; i < n; ++i) {
            DigitDecision d{digits[i].place, true, false,
                            periodicAudit ? "periodic full audit"
                                          : (noModel ? "no physics model" : "no elapsed-time reference")};
            plan.digits.push_back(d);
            plan.plannedReads++;
        }
        return plan;
    }

    // Expected maximum value increment since the last confirmed reading, with the safety margin.
    const double margin = limits.predictionSafetyFactor > 0.0 ? limits.predictionSafetyFactor : 1.0;
    plan.maxDelta = plan.bounds.expectedMaxPerMin * plan.minutesElapsed * margin;

    // The most significant place the increment can directly reach: 10^place <= maxDelta.
    if (plan.maxDelta > 0.0) {
        plan.highestReachablePlace = (int)std::floor(std::log10(plan.maxDelta));
    } else {
        plan.highestReachablePlace = -9999; // nothing can have changed
    }

    // Walk LSD -> MSD tracking whether a carry into the next digit up is possible.
    // A carry into place p+1 is possible only if the digit at place p, plus the increment that can
    // still be applied at that place, can reach/exceed 10 - i.e. the digit is close enough to its
    // wrap point that the expected max increment could push it over. We only trust a "no carry, so
    // skip the digit above" conclusion when the lower digit was read confidently.
    bool carryPossible = true;       // until proven otherwise (the LSD always reads)
    bool lowerConfident = true;      // confidence chain holds so far

    for (size_t i = 0; i < n; ++i) {
        const DigitState& ds = digits[i];
        const double placeUnit = std::pow(10.0, ds.place);

        bool mustRead;
        const char* reason;

        // The increment capacity expressed in *this digit's* units: how many steps of this digit
        // the expected max increment represents.
        const double stepsAtThisPlace = (placeUnit > 0.0) ? (plan.maxDelta / placeUnit) : 0.0;

        if (ds.place <= plan.highestReachablePlace) {
            // The increment can directly drive this digit -> it may have changed.
            mustRead = true;
            reason = "within reach of max increment";
        } else if (!ds.cacheValid) {
            // Nothing trustworthy to fall back on.
            mustRead = true;
            reason = "no cached value";
        } else if (carryPossible && lowerConfident) {
            // Out of direct reach, but a carry from below could still tick this digit.
            mustRead = true;
            reason = "carry possible from below";
        } else if (!lowerConfident) {
            // We couldn't trust the lower digit, so we can't rule out a carry: read to be safe.
            mustRead = true;
            reason = "low confidence below";
        } else {
            // Out of reach AND no carry can arrive AND lower digits were confident: provably static.
            mustRead = false;
            reason = "physically cannot change";
        }

        DigitDecision dec{ds.place, mustRead,
                          /*carryWatch*/ (carryPossible && ds.place > plan.highestReachablePlace),
                          reason};
        plan.digits.push_back(dec);
        if (mustRead) plan.plannedReads++;

        // Compute whether a carry/borrow can propagate INTO the next-higher digit. On an increase the
        // digit wraps when it can reach 10 (headroom up = 10 - value); on a decrease (allowNegative) it
        // wraps when it can reach 0 (headroom down = value + 1). A bidirectional sequence wraps if it
        // can reach EITHER boundary, so use the nearer one.
        const double headroomUp   = 10.0 - (double)ds.value;
        const double headroomDown = (double)ds.value + 1.0;
        const double headroom = allowNegative ? (headroomUp < headroomDown ? headroomUp : headroomDown) : headroomUp;
        const bool thisCanWrap = (stepsAtThisPlace >= headroom);
        carryPossible = thisCanWrap;                 // carry/borrow into next digit only if this one can wrap
        lowerConfident = lowerConfident && (ds.confidence >= confidenceFloor);
    }

    return plan;
}

// --- Per-digit confident-read history -------------------------------------------------------------

void DigitHistory::addConfident(int v) {
    if (v < 0 || v > 9) return;            // only genuine confident reads enter the matrix
    buf_[head_] = v;
    head_ = (head_ + 1) % CAP;
    if (count_ < CAP) count_++;
}

int DigitHistory::at(int idxFromOldest) const {
    int start = (head_ - count_ + CAP) % CAP;
    return buf_[(start + idxFromOldest) % CAP];
}

bool DigitHistory::mostRecent(int& out) const {
    if (count_ == 0) return false;
    out = at(count_ - 1);
    return true;
}

bool DigitHistory::mode(int& out, int& votes) const {
    if (count_ == 0) return false;
    int tally[10] = {0};
    for (int i = 0; i < count_; ++i) tally[at(i)]++;
    int best = 0;
    for (int d = 1; d < 10; ++d) if (tally[d] > tally[best]) best = d;
    out = best;
    votes = tally[best];
    return true;
}

bool DigitHistory::majority(int& out) const {
    int v, votes;
    if (!mode(v, votes)) return false;
    if (votes * 2 > count_) { out = v; return true; }
    return false;
}

int DigitHistory::snapshot(int* dst, int maxn) const {
    int n = (count_ < maxn) ? count_ : maxn;
    for (int k = 0; k < n; ++k) dst[k] = at(count_ - 1 - k);   // newest first
    return n;
}

bool resolveUnknownDigit(const DigitHistory& h, bool canIncrement, bool lowerNeighborChanged, int& out) {
    if (h.empty()) return false;            // no confident reading ever -> stays "N"
    if (!canIncrement) {                    // cannot change -> trust the steady-state history
        if (h.majority(out)) return true;
        return h.mostRecent(out);
    }
    // can increment: if the digit below is stable, no carry could have reached us -> assume unchanged.
    // if the digit below changed, we genuinely cannot know - provide the most recent confident value
    // as a best effort so every digit still yields a valid integer.
    (void)lowerNeighborChanged;
    return h.mostRecent(out);
}

// --- Physical plausibility ------------------------------------------------------------------------

Plausibility checkPlausibility(const PhysicalLimits& limits,
                               double previousValue,
                               double newValue,
                               double minutesElapsed,
                               bool allowNegative) {
    const RateBounds rb = deriveRateBounds(limits);
    if (!rb.known || rb.ceilingPerMin < 0.0) return Plausibility::Unknown;

    const double delta = newValue - previousValue;

    // A decrease is rejected outright only when negatives are not allowed; when they are (e.g. a
    // flow-rate display), a decrease is permitted but its MAGNITUDE is still bounded below.
    if (!allowNegative && delta < 0.0) return Plausibility::NegativeChange;

    if (minutesElapsed <= 0.0) {
        // No time reference: the magnitude can't be judged, so treat the change as plausible.
        return Plausibility::Plausible;
    }

    // Symmetric physical ceiling over the elapsed time. With allowNegative the value may move the same
    // distance in either direction (|delta|); without it, delta is already >= 0 here.
    const double maxPossible = rb.ceilingPerMin * minutesElapsed;
    const double magnitude = (delta < 0.0) ? -delta : delta;
    if (magnitude > maxPossible) return Plausibility::ExceedsPhysicalMax;
    return Plausibility::Plausible;
}

} // namespace predictive
