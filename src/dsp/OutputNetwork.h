#pragma once

#include <algorithm>

#include <chowdsp_wdf/chowdsp_wdf.h>

#include "CircuitValues.h"
#include "utils/TaperUtils.h"

namespace pedal::dsp
{
/**
 * Stage 3 -- output / VOLUME network (circuit.md), driven by the JFET drain's Norton current.
 *
 *   i_d -->  D  --[C10 100n]--  E  --[R9 110k]-- OUT --[R8 110k]-- P3
 *            |                  |                              |
 *      [R6||ro]            [R10 240k]                       [Rb]
 *            |                  |     [Ra]                     |
 *           GND                GND     |                      GND
 *                                     GND
 *
 * Ra = VOLUME lug1 -> wiper, Rb = wiper -> lug3, Ra + Rb = 500 k always. The WIPER IS GROUNDED and
 * both END lugs feed signal nodes -- verified twice at high zoom. This is not the usual
 * wiper-to-output divider, and it is what makes the control law deliberately NON-MONOTONIC: silence
 * at full CCW, a peak near 1-2 o'clock, then a fall-back. That is correct EP-3 wiring, confirmed
 * against the maker's published control description. Do not "fix" it into a conventional divider.
 *
 * NOTE ON TOPOLOGY, because circuit.md's wording invites the opposite conclusion: R9 is called a
 * "bridging" resistor and warned against being modelled "as a series element in a simple divider".
 * That warning is about the CONTROL LAW, not the graph. Electrically this is a plain series/parallel
 * TREE -- from node E there are exactly three paths to ground (R10, Ra, and the R9+R8+Rb chain with
 * OUT tapped inside it) -- so it needs no R-type adaptor and no scattering matrix. The coupling that
 * matters is that Ra and Rb are two halves of ONE physical pot moving in opposite directions, which
 * is a parameter coupling, handled by updating both under a single deferred impedance propagation.
 *
 * Verified against circuit.md's own published table: at Ra = 176 k the network gain is -3.79 dB
 * (its peak) and at Ra = 500 k it is -7.71 dB, a 3.92 dB fall-back. OutputNetworkTest asserts both.
 */
class OutputNetwork
{
public:
    void prepare(double sampleRate)
    {
        // C10's corner moves with the volume setting (13 Hz near the top of the range, ~54 Hz wound
        // down), which is why it is solved inside the network rather than pre-computed. It is
        // nowhere near Nyquist at any setting, so it is not prewarped.
        c10.prepare(sampleRate);
    }

    void reset() { c10.reset(); }

    /** Drain Norton impedance from JfetStage::outputImpedance(), in ohms. */
    void setDrainImpedance(double rOutOhms) { rOut.setResistanceValue(rOutOhms); }

    /** VOLUME knob, 0..1. Full CCW grounds node E through Ra -> 0 and genuinely silences the pedal. */
    void setVolume(double x)
    {
        const double ra = std::clamp(pedal::taper::powerLawTaper(x, circuit::kVolumePot, circuit::kVolumeTaperP),
                                     kMinPotArm, circuit::kVolumePot - kMinPotArm);

        // Ra and Rb are one physical control and must never be updated one at a time -- propagating
        // impedance between the two writes would solve a network the pot can never actually be in.
        //
        // Defer at exactly ONE barrier: pRa, the lowest common ancestor of rA and rB. The scoped
        // guard is a HARD barrier (a deferred element neither recalculates nor propagates) and its
        // destructor recalculates only the elements it was given, in the order given -- so listing
        // the whole chain would recalculate parents before their children and leave stale impedances
        // everywhere. One barrier plus one manual propagation above it is the documented pattern.
        {
            chowdsp::wdft::ScopedDeferImpedancePropagation deferImpedance { pRa };
            rA.setResistanceValue(ra);
            rB.setResistanceValue(circuit::kVolumePot - ra);
        }
        pNodeE.propagateImpedanceChange();
    }

    /** Drain Norton current (amps, injected into node D) -> output volts at the jack. */
    inline double processSample(double iDrain) noexcept
    {
        cs.setCurrent(iDrain);
        cs.incident(pDrain.reflected());
        pDrain.incident(cs.reflected());
        return chowdsp::wdft::voltage<double>(sOut);
    }

private:
    // A pot arm never reaches a true 0 ohm, and a zero-impedance port would divide by zero in the
    // parallel adaptor. 0.1 ohm puts full CCW ~106 dB down, which is silence by any measure.
    static constexpr double kMinPotArm = 0.1;

    chowdsp::wdft::ResistorT<double> rOut { circuit::kR6 };
    chowdsp::wdft::CapacitorT<double> c10 { circuit::kC10 };
    chowdsp::wdft::ResistorT<double> r10 { circuit::kR10 };
    chowdsp::wdft::ResistorT<double> rA { circuit::kVolumePot * 0.5 };
    chowdsp::wdft::ResistorT<double> r9 { circuit::kR9 };
    chowdsp::wdft::ResistorT<double> r8 { circuit::kR8 };
    chowdsp::wdft::ResistorT<double> rB { circuit::kVolumePot * 0.5 };

    // OUT -> GND through R8 then Rb. The voltage ACROSS this composite two-port is V(OUT), which is
    // read from passive ports only -- never from the source port, whose wave is scheduled a sample
    // apart and would add a spurious 2-point-average lowpass (dsp.md).
    chowdsp::wdft::WDFSeriesT<double, decltype(r8), decltype(rB)> sOut { r8, rB };
    chowdsp::wdft::WDFSeriesT<double, decltype(r9), decltype(sOut)> sBranch { r9, sOut };
    chowdsp::wdft::WDFParallelT<double, decltype(rA), decltype(sBranch)> pRa { rA, sBranch };
    chowdsp::wdft::WDFParallelT<double, decltype(r10), decltype(pRa)> pNodeE { r10, pRa };
    chowdsp::wdft::WDFSeriesT<double, decltype(c10), decltype(pNodeE)> sC10 { c10, pNodeE };
    chowdsp::wdft::WDFParallelT<double, decltype(rOut), decltype(sC10)> pDrain { rOut, sC10 };
    chowdsp::wdft::IdealCurrentSourceT<double, decltype(pDrain)> cs { pDrain };
};
} // namespace pedal::dsp
