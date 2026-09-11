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
 *            |                  |                  |           |
 *      [R6||ro]            [R10 240k]           [RL]        [Rb]
 *            |                  |     [Ra]         |           |
 *           GND                GND     |          GND         GND
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
    void setDrainImpedance(double rOutOhms)
    {
        rOut.setResistanceValue(rOutOhms);
        lastRout = rOutOhms;
    }

    /** Resistive load at the output jack, in ohms. kNoLoad is effectively open.
     *
     *  ⭐⭐ THIS MATTERS FAR MORE ON THIS PEDAL THAN ON A NORMAL ONE, and that is a property of the
     *  circuit rather than a modelling choice. The VOLUME wiper is GROUNDED and R9 = 110 k bridges
     *  node E to the jack, so the pot cannot pull the output impedance down the way a conventional
     *  wiper-to-output divider would: it measures 59-102 kOhm across the rotation (circuit.md
     *  note #22, measured, model correct to 2 %). A load therefore does two things, and neither is
     *  a constant:
     *
     *    - it attenuates, and since Zout MOVES with the knob (101 k at 10:30, 59 k at full CW) it
     *      attenuates unevenly -- it TILTS the control law. The as-drawn peak-to-full-CW fall-back
     *      of 3.9 dB becomes 1.7 dB into 75 kOhm.
     *    - it lowers the impedance at node E, which raises C10's high-pass corner (14.6 -> 18.3 Hz
     *      at 1:30 into 75 kOhm) and lowers the DRAIN-node impedance by up to 6 %, which is the
     *      load line's slope -- so it moves the triode onset too (+0.45 dB at 1:30).
     *
     *  ⛔ It does NOT change the response above 100 Hz at all (a resistive load is flat there), and
     *  it does not change the harmonics below ~2 V of gate drive. Measured: 0.00 dB and 0.000 dB.
     *
     *  📌 WHY THIS EXISTS AT ALL, since the project decided in 2026-09-08 to ship no load. P4's own
     *  captures put the pedal at +10.6 dB (dark) / +11.9 dB (bright) at 1:30 into the interface's
     *  1 MOhm -- a measurement that is a ratio against a bare loop, so it depends on no calibration
     *  figure and is very hard to be wrong. Yet the maker's published control law AND, per the owner
     *  2026-09-11, the consensus of many independent user reviews and videos all describe ~3 dB.
     *  A single load in the 68-82 kOhm band reconciles every one of the maker's four published
     *  claims at once (see circuit.md note #31). The pedal's own output impedance is what makes that
     *  possible, so this is a real physical effect that was deliberately omitted, not a fudge --
     *  but its VALUE is fitted to those claims and cannot be identified from the circuit, which is
     *  why it is a user control with a documented default rather than a baked-in constant. */
    void setLoad(double ohms)
    {
        const double rl = std::max(ohms, kMinPotArm);
        rLoad.setResistanceValue(rl);
        lastLoad = rl;
        pNodeE.propagateImpedanceChange();
    }

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
        lastRa = ra;
    }

    /** AC impedance seen looking INTO the drain node, in ohms. This is the load line's slope, and
     *  JfetStage needs it because that is what turns drain current into drain volts.
     *
     *  Computed with C10 treated as a short, i.e. the mid-band value. ⚠ That is an approximation at
     *  the bottom: below ~100 Hz C10 starts blocking, which RAISES this impedance and so brings the
     *  drain into triode slightly EARLIER than the model will predict. Measured spread over
     *  50 Hz - 5 kHz: 0.3 kOhm at VOLUME 0.8 (negligible) but 5.8 kOhm at VOLUME 0.2, where it is
     *  worth about 2 dB of onset level. The frequency term is left out because the stage runs at the
     *  OVERSAMPLED rate while this network runs at base rate, so a per-sample drain voltage is not
     *  available to it even in principle -- see JfetParams::zLoad.
     *
     *  The VOLUME term is kept because it is the larger one: this value runs 9.8 to 17.9 kOhm across
     *  the rotation, which moves the triode onset by about 4 dB. A fixed constant would have put the
     *  load line in the wrong place at one end of the knob or the other. */
    double drainNodeImpedance() const
    {
        const double ra = lastRa;
        const double rb = circuit::kVolumePot - ra;
        // ⚠ The jack load belongs in here, not only in the audio path: it lowers the impedance at
        // node E, which lowers this, which IS the load line's slope. Worth up to 6 % (17.4 -> 16.3
        // kOhm at 1:30 into 75 kOhm) and so ~0.45 dB of triode onset. Omitting it would leave the
        // load audible in the linear response but silently absent from the clipping behaviour.
        const double rOutBranch = circuit::kR8 + rb;
        const double rJack = (lastLoad * rOutBranch) / (lastLoad + rOutBranch);
        const double zE = 1.0 / (1.0 / circuit::kR10 + 1.0 / ra + 1.0 / (circuit::kR9 + rJack));
        const double rd = lastRout;
        return (rd * zE) / (rd + zE);
    }

    /** Drain Norton current (amps, injected into node D) -> output volts at the jack. */
    inline double processSample(double iDrain) noexcept
    {
        cs.setCurrent(iDrain);
        cs.incident(pDrain.reflected());
        pDrain.incident(cs.reflected());
        return chowdsp::wdft::voltage<double>(pOut);
    }

private:
    // A pot arm never reaches a true 0 ohm, and a zero-impedance port would divide by zero in the
    // parallel adaptor. 0.1 ohm puts full CCW ~106 dB down, which is silence by any measure.
    static constexpr double kMinPotArm = 0.1;

    double lastRa = circuit::kVolumePot * 0.25;
    double lastRout = circuit::kR6;
    double lastLoad = circuit::kNoLoad;

    chowdsp::wdft::ResistorT<double> rOut { circuit::kR6 };
    chowdsp::wdft::CapacitorT<double> c10 { circuit::kC10 };
    chowdsp::wdft::ResistorT<double> r10 { circuit::kR10 };
    chowdsp::wdft::ResistorT<double> rA { circuit::kVolumePot * 0.5 };
    chowdsp::wdft::ResistorT<double> r9 { circuit::kR9 };
    chowdsp::wdft::ResistorT<double> r8 { circuit::kR8 };
    chowdsp::wdft::ResistorT<double> rB { circuit::kVolumePot * 0.5 };
    chowdsp::wdft::ResistorT<double> rLoad { circuit::kNoLoad };

    // OUT -> GND through R8 then Rb, in parallel with the jack load. The voltage ACROSS this
    // composite is V(OUT), which is read from passive ports only -- never from the source port,
    // whose wave is scheduled a sample apart and would add a spurious 2-point-average lowpass
    // (dsp.md). ⚠ The read moved from sOut to pOut when the load was added: reading sOut would
    // give the voltage across R8+Rb ALONE, which is the same node but stops being the whole story
    // the moment anything else hangs off it.
    chowdsp::wdft::WDFSeriesT<double, decltype(r8), decltype(rB)> sOut { r8, rB };
    chowdsp::wdft::WDFParallelT<double, decltype(rLoad), decltype(sOut)> pOut { rLoad, sOut };
    chowdsp::wdft::WDFSeriesT<double, decltype(r9), decltype(pOut)> sBranch { r9, pOut };
    chowdsp::wdft::WDFParallelT<double, decltype(rA), decltype(sBranch)> pRa { rA, sBranch };
    chowdsp::wdft::WDFParallelT<double, decltype(r10), decltype(pRa)> pNodeE { r10, pRa };
    chowdsp::wdft::WDFSeriesT<double, decltype(c10), decltype(pNodeE)> sC10 { c10, pNodeE };
    chowdsp::wdft::WDFParallelT<double, decltype(rOut), decltype(sC10)> pDrain { rOut, sC10 };
    chowdsp::wdft::IdealCurrentSourceT<double, decltype(pDrain)> cs { pDrain };
};
} // namespace pedal::dsp
