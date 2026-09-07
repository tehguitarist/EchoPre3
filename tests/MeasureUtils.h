#pragma once

#include <cmath>
#include <complex>
#include <utility>

// Shared steady-state response instrument for the per-stage tests.
//
// WHY NOT PEAK DETECTION: taking the peak sample of a sine is only exact when a sample lands on the
// waveform's crest. The output's phase shift generally moves the crest between samples, so the error
// is frequency- AND phase-dependent, up to 1 - cos(pi/samplesPerCycle) -- about 0.08 dB at 24
// samples/cycle and 0.5 dB at 9.6. That is large enough to masquerade as a coefficient bug (it did:
// an earlier version of JfetStageTest flagged a 0.053 dB "failure" at exactly one frequency) and
// large enough to mask a real regression elsewhere. Quadrature correlation has neither problem, and
// it yields the PHASE for free -- which magnitude-only testing throws away.

namespace pedal::test
{
/**
 * Complex steady-state response H(f) = Y(f)/X(f) of a linear (or small-signal-linearised) process,
 * by correlating the output against sin and cos at the drive frequency.
 *
 * For x = A*sin(wt) and y = |H|*A*sin(wt + phi), the sin-correlation recovers |H|cos(phi) and the
 * cos-correlation |H|sin(phi), so arg(H) = atan2(im, re) with the usual sign: lag is negative.
 *
 * The window spans at least 100 cycles and at least 0.5 s, keeping leakage from the window not being
 * a whole number of cycles well under 0.01 dB. The settling period must outlast the slowest time
 * constant under test (the ~6.5 Hz input high-pass is the slowest here).
 */
template <typename ProcessFn>
std::complex<double> measureResponse(ProcessFn&& process, double freq, double fs, double amplitude = 1.0)
{
    const int nSettle = (int) std::ceil(0.5 * fs);
    const int nMeasure = (int) std::ceil(std::max(0.5 * fs, 100.0 * fs / freq));

    double re = 0.0, im = 0.0;
    for (int n = 0; n < nSettle + nMeasure; ++n)
    {
        const double th = 2.0 * M_PI * freq * (double) n / fs;
        const double y = process(amplitude * std::sin(th));
        if (n >= nSettle)
        {
            re += y * std::sin(th);
            im += y * std::cos(th);
        }
    }
    const double scale = 2.0 / ((double) nMeasure * amplitude);
    return { re * scale, im * scale };
}

template <typename ProcessFn>
double measureGain(ProcessFn&& process, double freq, double fs, double amplitude = 1.0)
{
    return std::abs(measureResponse(std::forward<ProcessFn>(process), freq, fs, amplitude));
}

/**
 * The analog frequency a bilinear-discretised filter actually behaves like at digital frequency
 * `freq`. Comparing a WDF stage against its analytic prototype evaluated HERE isolates "are the
 * coefficients right" from "does bilinear warp exist"; comparing against the prototype at `freq`
 * measures the warp itself. The two questions need separating -- conflating them once made a
 * correct filter look like a coefficient bug.
 */
inline double warpedFrequency(double freq, double fs) { return (fs / M_PI) * std::tan(M_PI * freq / fs); }

inline double degrees(double radians) { return radians * 180.0 / M_PI; }

/** Smallest signed difference between two angles, in degrees. */
inline double phaseErrorDeg(double gotRad, double wantRad)
{
    double d = degrees(gotRad - wantRad);
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    return d;
}

/**
 * Excess delay implied by a phase error, in SAMPLES. This is the diagnostic that names the trap in
 * dsp.md: reconstructing a node voltage from a SOURCE port (rather than only passive ports) mixes
 * Vs[n] with Vs[n-1], which is a half-sample delay. That shows up as a smoothly drooping top end --
 * easily mistaken for ordinary bilinear cap warping -- but its signature is unmistakable here,
 * because a pure delay's phase error grows LINEARLY with frequency at a constant sample count.
 */
inline double excessDelaySamples(double phaseErrDeg, double freq, double fs)
{
    return -(phaseErrDeg / 360.0) * (fs / freq);
}
} // namespace pedal::test
