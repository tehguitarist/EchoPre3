// The low-oversampling top-octave restore (dsp.md, build-plan.md §12).
//
// This is the one stage in the chain that exists to CORRECT another stage rather than to model a
// component, so the assertions are shaped differently from the rest of the suite. There is no
// circuit to compare it against; what it must satisfy is:
//
//   1. it never makes the response WORSE than leaving it alone -- at any base rate, any factor,
//      any frequency. A restore that helps on average and hurts somewhere is not shippable, and
//      "worst case over the band" is the only form of that claim worth asserting;
//   2. its boost stays BOUNDED, because the thing it is inverting has a zero at Nyquist and the
//      unbounded design needs +29 to +37 dB there -- straight into 1x's alias products;
//   3. it is unity at DC and essentially unity through the midband, so it cannot become a level or
//      tone change;
//   4. ⭐ the model it is derived from is the one the plugin actually runs. The whole design rests
//      on InputNetwork::discretisationGain() being the real error of the real WDF tree, so that is
//      checked against the tree by measurement, not assumed.
//
// Point 4 is the one that could rot silently: the restore would go on correcting a network that no
// longer exists, and every other check here would still pass.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

#include "dsp/InputNetwork.h"
#include "dsp/OsDroopRestore.h"

#include "MeasureUtils.h"

namespace
{
bool ok = true;
void fail(const char* msg)
{
    std::printf("  <-- FAIL: %s\n", msg);
    ok = false;
}

double db(double x) { return 20.0 * std::log10(x); }

const double kBaseRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
const int kFactors[] = { 1, 2, 4, 8 };

// Magnitude of the restore at `freq` when it runs at `runRate`.
double restoreGain(pedal::dsp::OsDroopRestore& r, double freq, double runRate)
{
    r.reset();
    return pedal::test::measureGain([&r](double x) { return r.processSample(x); }, freq, runRate);
}
} // namespace

int main()
{
    using namespace pedal;

    // ---- 1. ⭐ The derivation's own premise: discretisationGain() must be the REAL error of the
    //         real WDF tree. Everything else in this file is downstream of it.
    std::printf("1. InputNetwork::discretisationGain() vs the WDF tree it claims to describe:\n");
    {
        double worst = 0.0;
        for (const double fs : { 48000.0, 96000.0, 192000.0 })
        {
            dsp::InputNetwork net;
            net.prepare(fs);
            for (const double f : { 1000.0, 4000.0, 8000.0, 12000.0, 16000.0, 20000.0 })
            {
                net.reset();
                const double measured =
                    pedal::test::measureGain([&net](double x) { return net.processSample(x); }, f, fs);
                const double predicted =
                    std::abs(dsp::InputNetwork::analyticResponse(f)) * dsp::InputNetwork::discretisationGain(f, fs);
                const double errDb = std::abs(db(measured / predicted));
                worst = std::max(worst, errDb);
            }
        }
        std::printf("   worst disagreement over 3 rates x 6 frequencies: %.4f dB\n", worst);
        if (worst > 0.02)
            fail("the restore is derived from a model that no longer matches the input network");
    }

    // ---- 2. It must never make anything worse, anywhere. The comparison is against doing nothing,
    //         frequency by frequency, so an improvement on average cannot hide a local regression.
    //         The allowance is not zero: a shelf that corrects a -3 dB droop will overshoot slightly
    //         somewhere, and forbidding that outright would forbid any first-order restore at all.
    constexpr double kAllowedOvershootDb = 0.45;
    std::printf("\n2. Restore vs doing nothing, worst |error| over 20 Hz - 20 kHz (dB vs analog):\n");
    std::printf("   %8s %5s %10s %10s %9s %8s\n", "base", "fac", "no restore", "restored", "peak dB", "bypassed");

    for (const double baseRate : kBaseRates)
        for (const int factor : kFactors)
        {
            const double osRate = baseRate * (double) factor;
            dsp::OsDroopRestore r;
            r.prepare(baseRate, osRate, osRate);

            // ⚠ The instrument's own floor, measured rather than assumed. `off` is computed in closed
            //    form while `on` is MEASURED through the filter, so only one of the two carries the
            //    correlation instrument's leakage -- and on the rows where the restore bypasses
            //    itself the whole droop is 0.004 dB, an order below that noise. Without this a
            //    correctly-bypassed restore reads as a 0.001 dB regression.
            dsp::OsDroopRestore identity; // default-constructed: unity, by member initialisation

            double worstOff = 0.0, worstOn = 0.0, worstRegression = 0.0, floorDb = 0.0;
            constexpr int kNumProbe = 40;
            const double fTop = std::min(20000.0, 0.45 * baseRate);
            for (int i = 0; i < kNumProbe; ++i)
            {
                const double f = 20.0 * std::pow(fTop / 20.0, (double) i / (double) (kNumProbe - 1));
                floorDb = std::max(floorDb, std::abs(db(restoreGain(identity, f, osRate))));
                const double off = db(dsp::InputNetwork::discretisationGain(f, osRate));
                const double on = off + db(restoreGain(r, f, osRate));
                worstOff = std::max(worstOff, std::abs(off));
                worstOn = std::max(worstOn, std::abs(on));
                // A regression is being FURTHER from flat than the uncorrected network was here.
                worstRegression = std::max(worstRegression, std::abs(on) - std::abs(off));
            }

            std::printf("   %7.1fk %4dx %10.3f %10.3f %9.2f %8s%s\n", baseRate / 1000.0, factor, worstOff,
                        worstOn, r.isBypassed() ? 0.0 : r.plateauDb(), r.isBypassed() ? "yes" : "no",
                        worstRegression > kAllowedOvershootDb ? "   <-- FAIL" : "");
            if (worstRegression > kAllowedOvershootDb)
                fail("the restore makes some frequency worse than leaving it alone");
            if (worstOn > worstOff + floorDb)
                fail("the restore does not reduce the worst-case error");
            // A bypassed restore must be the exact identity, so the allowance above can never be
            // covering for a filter that is quietly doing something on those rows.
            if (r.isBypassed() && (r.plateauDb() != 0.0 || worstOn > worstOff + floorDb))
                fail("a bypassed restore is not the identity");

            // ---- 3. Bounded boost, and no level or midband change.
            if (! r.isBypassed() && r.plateauDb() > 8.0)
                fail("restore boost exceeds its bound -- it would amplify 1x's alias products");
            for (const double f : { 100.0, 1000.0 })
                if (std::abs(db(restoreGain(r, f, osRate))) > 0.05)
                    fail("the restore is not transparent in the midband");
        }

    // ---- 4. It has to shrink to nothing as the factor rises, or the oversampling selector would
    //         still be changing the response for a reason that no longer exists.
    std::printf("\n3. The correction must shrink with the factor (48 kHz base):\n");
    double prev = 1.0e9;
    for (const int factor : kFactors)
    {
        dsp::OsDroopRestore r;
        r.prepare(48000.0, 48000.0 * factor, 48000.0 * factor);
        const double peak = r.isBypassed() ? 0.0 : r.plateauDb();
        std::printf("   %dx peak boost %.3f dB\n", factor, peak);
        if (peak >= prev)
            fail("the correction does not shrink as the oversampling factor rises");
        prev = peak;
    }

    std::printf(ok ? "\nPASS: droop restore\n" : "\nFAILED: droop restore\n");
    return ok ? 0 : 1;
}
