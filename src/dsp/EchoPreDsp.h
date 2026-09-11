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

    /** Closed-form solve (production) vs the safeguarded-Newton reference. Test/probe only. */
    void setUseClosedForm(bool b) noexcept { jfet.setUseClosedForm(b); }

    /** Overdrive at the quiescent point. Measurement hook, kept because several analysis scripts
     *  sweep it by name -- it still means the same PHYSICAL quantity, and is converted to the
     *  parameter the stage now stores (|Vp| = Vov * (1 + gm*R5/m)). Production never calls it. */
    void setVov(double v)
    {
        auto p = jfet.getParams();
        p.vp = v * (1.0 + p.gm * circuit::kR5 / p.mExp);
        setParams(p);
    }

    /** Pinch-off magnitude -- the stage's amplitude parameter. ⭐ It is also, exactly, the CUTOFF
     *  ONSET in gate volts, which is what a capture measures directly. Measurement hook only. */
    void setVp(double v)
    {
        auto p = jfet.getParams();
        p.vp = v;
        setParams(p);
    }

    /** Transfer-law exponent (2.0 = Shichman-Hodges; this pedal measures 1.60). Measurement hook. */
    void setExponent(double m)
    {
        auto p = jfet.getParams();
        p.mExp = m;
        setParams(p);
    }

    /** Transconductance -- the stage's other square-law parameter, and the one the mode shelf's
     *  K0 = 1 + gm*R5 is built from. Measurement hook only; production uses JfetParams::gm.
     *
     *  ⚠ gm AND THE AMPLITUDE PARAMETER ARE ONE FAMILY (circuit.md note #22a). Under the OLD
     *  (gm, Vov) parameterisation this was a live trap: Id0 = gm*Vov/m grows without bound, so at
     *  the shipped gm a Vov of 1.2 put the quiescent drain-source voltage NEGATIVE and an hour of
     *  renders measured a collapsing operating point instead of a curvature. ⭐ Parameterised on
     *  |Vp| (JfetParams::vp) it cannot happen: Id0 = gm*|Vp|/(m + gm*R5) is bounded above by
     *  |Vp|/R5 for any gm at all. Sweeping gm alone is now safe -- it re-solves the bias.
     *  ⭐ Setting gm here also rebuilds the shelf, because setParams() calls updateShelf() and the
     *  discrete source one-port is DERIVED from the shelf coefficients (circuit.md note #12). So a
     *  gm sweep moves the mode differential to match, which is exactly what fitting Vov against a
     *  unit with a different gm requires. */
    void setGm(double g)
    {
        auto p = jfet.getParams();
        p.gm = g;
        setParams(p);
    }

    /** Quiescent overdrive implied by the current triple. Diagnostic only. */
    double quiescentOverdrive() const { return jfet.getParams().vov(); }

    /** The operating point the current (gm, |Vp|, m) triple implies. Measurement/diagnostic only. */
    void operatingPoint(double& id0, double& idss, double& vdsQ, double& vpMag) const
    {
        const auto& p = jfet.getParams();
        id0 = p.id0();
        vdsQ = p.vdsQuiescent();
        vpMag = p.vpMagnitude();
        idss = p.idss();
    }

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

    /** Resistive load at the output jack, in ohms (circuit::kNoLoad for unloaded). ⭐ Also re-pushes
     *  the drain-node impedance, for the same reason setVolume does: the load changes node E's
     *  impedance, which changes the load line's slope. See OutputNetwork::setLoad. */
    void setLoad(double ohms)
    {
        outputNet.setLoad(ohms);
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
