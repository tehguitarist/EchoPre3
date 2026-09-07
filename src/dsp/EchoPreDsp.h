#pragma once

#include "InputNetwork.h"
#include "JfetStage.h"
#include "OutputNetwork.h"

namespace pedal::dsp
{
/**
 * One channel's full circuit chain. Split across the oversampling boundary:
 *
 *   [ oversampled ]  input network -> JFET stage -> drain Norton CURRENT
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
    }

    void reset()
    {
        inputNet.reset();
        jfet.reset();
        outputNet.reset();
    }

    void setParams(const JfetParams& p)
    {
        jfet.setParams(p);
        outputNet.setDrainImpedance(jfet.outputImpedance());
    }

    void setMode(Mode m) { jfet.setMode(m); }

    /** Antiderivative anti-aliasing on the JFET shaper. Policy lives in the processor (it is a
     *  function of the oversampling factor); this only carries the decision down. */
    void setAdaa(bool shouldUseAdaa) noexcept { jfet.setAdaa(shouldUseAdaa); }
    void setVolume(double x) { outputNet.setVolume(x); }

    /** Runs at the OVERSAMPLED rate. Volts at the input jack -> drain Norton current in amps. */
    inline double processOversampled(double volts) noexcept
    {
        return jfet.processSample(inputNet.processSample(volts));
    }

    /** Runs at the BASE rate. Drain Norton current -> volts at the output jack. */
    inline double processBase(double current) noexcept { return outputNet.processSample(current); }

private:
    InputNetwork inputNet;
    JfetStage jfet;
    OutputNetwork outputNet;
};
} // namespace pedal::dsp
