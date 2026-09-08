#pragma once

#include "InputNetwork.h"
#include "JfetStage.h"
#include "OsDroopRestore.h"
#include "OutputNetwork.h"

namespace pedal::dsp
{
/**
 * One channel's full circuit chain. Split across the oversampling boundary:
 *
 *   [ oversampled ]  input network -> droop restore -> JFET stage -> drain Norton CURRENT
 *   [  base rate  ]  output / VOLUME network -> output volts
 *
 * The value crossing the boundary is a CURRENT, not a voltage, and that is the whole point: the
 * degenerated common-source stage is a current source, so the MODE switch's HF lift only becomes
 * audible through the loading of the output network. Handing the output network a voltage instead
 * would double-count the lift (see the trap in JfetStage.h).
 *
 * The input network is oversampled with the nonlinearity rather than left at base rate: its ~7.3 kHz
 * corner is low relative to Nyquist and prewarp alone still leaves ~2.1 dB of droop at 12 kHz. The
 * output network stays at base rate because its only cap, C10, sits at 13-54 Hz -- nowhere near
 * Nyquist, so there is nothing for oversampling to fix there.
 */
class EchoPreDsp
{
public:
    /** `osRate` is the oversampled rate (base rate x factor); `baseRate` the host's. */
    void prepare(double baseRate, double osRate)
    {
        inputNet.prepare(osRate);
        jfet.prepare(osRate);
        outputNet.prepare(baseRate);
        outputNet.setDrainImpedance(jfet.outputImpedance());
        jfet.setDrainLoad(outputNet.drainNodeImpedance());
        droopRestore.prepare(baseRate, osRate, osRate);
    }

    void reset()
    {
        inputNet.reset();
        jfet.reset();
        outputNet.reset();
        droopRestore.reset();
    }

    void setParams(const JfetParams& p)
    {
        jfet.setParams(p);
        outputNet.setDrainImpedance(jfet.outputImpedance());
        jfet.setDrainLoad(outputNet.drainNodeImpedance());
    }

    void setMode(Mode m) { jfet.setMode(m); }

    /** Newton iterations per sample in the JFET solve. Production never calls this -- kSolveIters is
     *  the shipped value. It exists so FeatureProfile can A/B the count, which is the one real
     *  CPU-versus-accuracy lever this chain still has now ADAA is gone. */
    void setSolveIters(int n) noexcept { jfet.setSolveIters(n); }

    /** Antiderivative anti-aliasing on the JFET shaper. Policy lives in the processor (it is a
     *  function of the oversampling factor); this only carries the decision down. */
    /** AC impedance at the drain node, which sets the load line's slope inside the JFET stage.
     *  Per BLOCK, alongside VOLUME, because the stage runs at the oversampled rate while the output
     *  network runs at base rate -- the real drain voltage is not available per oversampled sample
     *  even in principle. See JfetParams::zLoad. */

    /** VOLUME. ⭐ Also pushes the drain-node impedance into the JFET stage, because that impedance
     *  IS the load line's slope and it moves with this knob: 9.8 kOhm at the bottom of the rotation
     *  to 17.9 at the top, which is about 4 dB of where the drain enters triode. Coupling the two
     *  here rather than fixing a constant is the difference between the load line being in the right
     *  place at one end of the knob and at both. */
    void setVolume(double x)
    {
        outputNet.setVolume(x);
        jfet.setDrainLoad(outputNet.drainNodeImpedance());
    }

    /** Runs at the OVERSAMPLED rate. Volts at the input jack -> drain Norton current in amps.
     *
     *  The droop restore sits BETWEEN the input network and the JFET, undoing the input network's own
     *  discretisation error before the shaper ever sees it. That position was chosen by measurement,
     *  not by reading dsp.md's "one biquad at base rate" literally: correcting after the chain fixes
     *  the linear path equally well but also boosts the HARMONICS, which never carried the droop --
     *  worth 0.6 dB of wanted H2 and 1.5 dB of alias floor at 1x. See OsDroopRestore.h. It
     *  self-disables when there is nothing left to correct. */
    inline double processOversampled(double volts) noexcept
    {
        return jfet.processSample(droopRestore.processSample(inputNet.processSample(volts)));
    }

    /** Runs at the BASE rate. Drain Norton current -> volts at the output jack. */
    inline double processBase(double current) noexcept { return outputNet.processSample(current); }

private:
    InputNetwork inputNet;
    JfetStage jfet;
    OutputNetwork outputNet;
    OsDroopRestore droopRestore;
};
} // namespace pedal::dsp
