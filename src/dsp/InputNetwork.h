#pragma once

#include <chowdsp_wdf/chowdsp_wdf.h>

#include "CircuitValues.h"
#include "utils/Prewarp.h"

namespace pedal::dsp
{
/**
 * Stage 1 -- input network (circuit.md). Purely linear, a plain series/parallel tree:
 *
 *   IN --[R3 110k]-- A --[C4 22n]-- G (gate)
 *                    |              |
 *                 [C3 220p]      [R4 1M]
 *                    |              |
 *                   GND            GND
 *
 * The JFET gate draws no current, so this network is unloaded and its output is simply V(G).
 *
 * Corners: HP C4/R4 ~ 7.2 Hz; LP R3 with C3 loaded by R4 ~ 7.3 kHz; passband divider
 * R4/(R3+R4) = -0.90 dB.
 *
 * WHERE THIS RUNS: inside the oversampled region, upstream of the JFET stage. C3's ~7.3 kHz corner
 * is low relative to Nyquist at 48 kHz, and prewarping alone does not rescue it -- prewarp pins the
 * corner exactly but cannot invert the bilinear transform's zero at Nyquist, which still costs about
 * 2.1 dB at 12 kHz (measured; InputNetworkTest reports it). Oversampling fixes the whole band, and
 * this stage is only four elements, so extending the region upstream to cover it is nearly free.
 *
 * The prewarp is kept and is computed from the rate this stage ACTUALLY runs at, so it corrects the
 * full amount at 1x and self-disables as the factor rises (a 0.5% correction at 4x). That is the
 * distinction dsp.md draws: prewarping at the BASE rate while running oversampled would over-correct;
 * prewarping at the running rate is always right. C4 (7.2 Hz) is nowhere near Nyquist and is left alone.
 */
class InputNetwork
{
public:
    void prepare(double sampleRate)
    {
        // The LP corner C3 forms with its surrounding resistance: R3 in parallel with R4, since C4 is
        // effectively a short at that frequency. NOT 1/(2*pi*R3*C3) of the cap alone (Prewarp.h).
        constexpr double rShunt = (circuit::kR3 * circuit::kR4) / (circuit::kR3 + circuit::kR4);
        const double cornerHz = 1.0 / (2.0 * M_PI * rShunt * circuit::kC3);

        c3.prepare(sampleRate);
        c4.prepare(sampleRate);
        c3.setCapacitanceValue(prewarpCapacitance(circuit::kC3, cornerHz, sampleRate));
    }

    void reset()
    {
        c3.reset();
        c4.reset();
    }

    /** Volts in at the jack -> volts at the JFET gate. */
    inline double processSample(double vIn) noexcept
    {
        vs.setVoltage(vIn);
        vs.incident(sIn.reflected());
        sIn.incident(vs.reflected());
        // Read across R4, a PASSIVE port. Never reconstruct a node voltage from the source port: its
        // wave is scheduled a sample apart, which adds a half-sample delay (dsp.md). The phase test
        // guards this explicitly.
        //
        // NO PolarityInverterT here. This is a passive RC ladder and physically cannot invert; the
        // inverter that the chowdsp smoke-test idiom uses put a clean 180 degrees on the output,
        // which magnitude testing cannot see at all. It would have cancelled the JFET stage's own
        // (correct, physical) inversion and left the whole plugin the wrong way round -- visible
        // only as a failed null against the reference renders, long after the fact.
        return chowdsp::wdft::voltage<double>(r4);
    }

private:
    chowdsp::wdft::ResistorT<double> r3 { circuit::kR3 };
    chowdsp::wdft::CapacitorT<double> c3 { circuit::kC3 };
    chowdsp::wdft::CapacitorT<double> c4 { circuit::kC4 };
    chowdsp::wdft::ResistorT<double> r4 { circuit::kR4 };

    // A -> GND via C4 then R4 (the gate leg), in parallel with C3, fed from IN through R3.
    chowdsp::wdft::WDFSeriesT<double, decltype(c4), decltype(r4)> sGate { c4, r4 };
    chowdsp::wdft::WDFParallelT<double, decltype(c3), decltype(sGate)> pNodeA { c3, sGate };
    chowdsp::wdft::WDFSeriesT<double, decltype(r3), decltype(pNodeA)> sIn { r3, pNodeA };
    chowdsp::wdft::IdealVoltageSourceT<double, decltype(sIn)> vs { sIn };
};
} // namespace pedal::dsp
