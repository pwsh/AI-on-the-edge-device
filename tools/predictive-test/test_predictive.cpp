#include "ClassPredictiveReader.h"
#include <cstdio>
#include <cassert>
#include <cmath>
#include <vector>
using namespace predictive;

static int failures = 0;
#define CHECK(cond) do { if(!(cond)){ printf("  FAIL: %s (line %d)\n", #cond, __LINE__); failures++; } } while(0)
#define APPROX(a,b,tol) (std::fabs((a)-(b)) <= (tol))

static std::vector<DigitState> makeDigits(const std::vector<int>& valsLsdFirst, float conf, bool cache=true) {
    std::vector<DigitState> d;
    for (size_t i = 0; i < valsLsdFirst.size(); ++i)
        d.push_back(DigitState{(int)i, valsLsdFirst[i], conf, cache});
    return d;
}

int main() {
    printf("== physics sanity ==\n");
    // Water: 3/4" (19.05mm), 60psi(410kPa). Torricelli v = sqrt(2*410000/1000)=28.6 m/s.
    // area = pi*(0.009525)^2 = 2.85e-4 m^2. ceiling m3/s = 8.16e-3 -> Lpm ~ 489.
    double lpmCeil = waterFlowLpm(19.05, 410.0, 0.0);
    printf("  water ceiling Lpm = %.1f\n", lpmCeil);
    CHECK(lpmCeil > 400 && lpmCeil < 600);
    // Expected with 3 m/s cap: m3/s = 2.85e-4*3 = 8.55e-4 -> Lpm ~ 51.
    double lpmExp = waterFlowLpm(19.05, 410.0, 3.0);
    printf("  water expected Lpm (3 m/s cap) = %.1f\n", lpmExp);
    CHECK(lpmExp > 40 && lpmExp < 65);
    CHECK(lpmExp < lpmCeil);

    // Electricity 200A*240V = 48kW -> 0.8 kWh/min.
    double kwhmin = electricEnergyKwhPerMin(240.0, 200.0);
    printf("  elec kWh/min = %.3f\n", kwhmin);
    CHECK(APPROX(kwhmin, 0.8, 1e-9));

    printf("== rate derivation (water, m^3 display) ==\n");
    PhysicalLimits w; w.utility = Utility::Water; w.unitsPerValue = 1000.0; // value in m^3
    RateBounds wb = deriveRateBounds(w);
    printf("  expected=%.5f m3/min  ceiling=%.5f m3/min\n", wb.expectedMaxPerMin, wb.ceilingPerMin);
    CHECK(wb.known);
    CHECK(APPROX(wb.expectedMaxPerMin, lpmExp/1000.0, 1e-9));
    CHECK(wb.ceilingPerMin > wb.expectedMaxPerMin);

    printf("== generic => no model => read all ==\n");
    PhysicalLimits g; g.utility = Utility::Generic;
    ReadPlan gp = planRead(g, makeDigits({5,5,5,5}, 1.0f), 10.0, false);
    CHECK(!gp.bounds.known);
    CHECK(gp.plannedReads == 4);

    printf("== predictive gating (water) ==\n");
    // Value display in m^3 with the integer digits being m^3. expected ~0.051 m3/min.
    // Over 5 minutes maxDelta = 0.051*5*1.5 ~= 0.385 m^3. log10(0.385) = -0.41 -> highest reachable
    // place = -1 (tenths). So ones/tens/... cannot directly change; only decimals can.
    // Use digits places -3..2 (m^3 with 3 decimals). LSD first: place -3,-2,-1,0,1,2
    std::vector<DigitState> d;
    int places[] = {-3,-2,-1,0,1,2};
    int vals[]   = { 4, 2, 3, 7, 1, 0}; // 017.234 -> none near a wrap at the high end
    for (int i=0;i<6;i++) d.push_back(DigitState{places[i], vals[i], 1.0f, true});
    ReadPlan p = planRead(w, d, 5.0, false);
    printf("  maxDelta=%.4f highestReachablePlace=%d plannedReads=%d\n", p.maxDelta, p.highestReachablePlace, p.plannedReads);
    for (auto& dec : p.digits)
        printf("    place %2d: %s (%s)\n", dec.place, dec.mustRead?"READ":"skip", dec.reason);
    CHECK(p.highestReachablePlace == -1);
    // places -3,-2,-1 within reach => READ. place 0 (ones=7): carry possible? steps at place0 =
    // maxDelta/1 = 0.385 < headroom(10-7=3) -> cannot wrap, BUT place 0 itself: is a carry possible
    // INTO it from place -1? place -1 value=3, steps at place -1 = 0.385/0.1=3.85 >= headroom 7? no
    // (3.85<7) so no carry into ones -> ones provably static => skip. Good test.
    // Find decisions:
    auto dec = [&](int place)->const DigitDecision*{ for (auto&x:p.digits) if(x.place==place) return &x; return nullptr; };
    CHECK(dec(-3)->mustRead && dec(-2)->mustRead && dec(-1)->mustRead);
    CHECK(!dec(0)->mustRead);   // ones cannot change and no carry can reach it
    CHECK(!dec(1)->mustRead);
    CHECK(!dec(2)->mustRead);

    printf("== carry propagation when a lower digit is near wrap ==\n");
    // Make tenths = 9 (near wrap) so a carry into ones is possible.
    int vals2[] = { 4, 2, 9, 9, 1, 0}; // .299? place-1=9, place0=9
    std::vector<DigitState> d2;
    for (int i=0;i<6;i++) d2.push_back(DigitState{places[i], vals2[i], 1.0f, true});
    ReadPlan p2 = planRead(w, d2, 5.0, false);
    // steps at place -1 = 3.85 >= headroom(10-9=1) -> CAN wrap -> carry into ones possible -> ones READ.
    // ones value=9, steps at place0 = 0.385 >= headroom 1? no -> no carry into tens -> tens skip.
    auto dec2 = [&](int place)->const DigitDecision*{ for (auto&x:p2.digits) if(x.place==place) return &x; return nullptr; };
    printf("  ones mustRead=%d tens mustRead=%d\n", dec2(0)->mustRead, dec2(1)->mustRead);
    CHECK(dec2(0)->mustRead);    // carry possible from tenths
    CHECK(!dec2(1)->mustRead);   // ones can't wrap this round -> tens static

    printf("== low confidence forces escalation ==\n");
    // Same as first water test but tenths read with low confidence -> can't rule out carry upward.
    std::vector<DigitState> d3;
    for (int i=0;i<6;i++) d3.push_back(DigitState{places[i], vals[i], (places[i]==-1?0.2f:1.0f), true});
    ReadPlan p3 = planRead(w, d3, 5.0, false);
    auto dec3 = [&](int place)->const DigitDecision*{ for (auto&x:p3.digits) if(x.place==place) return &x; return nullptr; };
    printf("  ones mustRead=%d (expect 1 due to low-confidence tenths)\n", dec3(0)->mustRead);
    CHECK(dec3(0)->mustRead);

    printf("== plausibility (physical ceiling) ==\n");
    // ceiling ~0.489 m3/min. Over 1 min, a +0.4 jump is plausible; +5.0 is impossible.
    CHECK(checkPlausibility(w, 100.0, 100.4, 1.0) == Plausibility::Plausible);
    CHECK(checkPlausibility(w, 100.0, 105.0, 1.0) == Plausibility::ExceedsPhysicalMax);
    CHECK(checkPlausibility(w, 100.0, 99.0, 1.0) == Plausibility::NegativeChange);
    CHECK(checkPlausibility(g, 100.0, 999.0, 1.0) == Plausibility::Unknown);

    printf("== user override of max rate ==\n");
    PhysicalLimits u; u.utility = Utility::Water; u.userMaxRatePerMin = 2.0;
    RateBounds ub = deriveRateBounds(u);
    CHECK(ub.known && APPROX(ub.expectedMaxPerMin,2.0,1e-9) && APPROX(ub.ceilingPerMin,2.0,1e-9));

    printf("== rolling history ==\n");
    RollingHistory h;
    h.add(100.0, 0);
    h.add(100.5, 60);    // +0.5 in 60s
    h.add(101.2, 120);   // +0.7 in 60s
    double r; CHECK(h.observedRatePerMin(r)); printf("  observed rate = %.3f/min\n", r);
    CHECK(APPROX(r, (101.2-100.0)/2.0, 1e-9));
    double pk; CHECK(h.peakRatePerMin(pk)); printf("  peak rate = %.3f/min\n", pk);
    CHECK(APPROX(pk, 0.7, 1e-9));
    CHECK(h.size()==3 && APPROX(h.latest().value,101.2,1e-9) && APPROX(h.oldest().value,100.0,1e-9));

    printf("\n%s (%d failures)\n", failures==0?"ALL PASS":"FAILURES", failures);
    return failures==0?0:1;
}
