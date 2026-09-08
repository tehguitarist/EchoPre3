// OfflineRender -- renders a WAV through the plugin, for the analysis/ A/B harness.
//
//   ./build/OfflineRender_artefacts/Release/OfflineRender <in.wav> <out.wav> \
//         [--os 1|2|4|8] [--volume 0..1] [--mode bright|dark|mid] \
//         [--input-trim dB] [--output-trim dB] [--input-scale dB] [--bypass 0|1] [--block N]
//         [--vov V]
//
// The CLI contract is fixed by the callers already in the tree: comprehensive_report.py,
// farina_validate.py, hf_thd_flatness_check.py, knob_tolerant_null.py and
// base_rate_warp_measure.py all invoke it as `<bin> <in> <out> --os <factor>` followed by the
// flags analysis/captures.py's render_args() emits (`--volume`, `--mode`). Positional in/out
// first, factor (not index) for --os.
//
// ================= WHY THIS INSTANTIATES THE REAL PROCESSOR =================
// build.md describes this exe as one that "mirrors processBlock". It does not mirror it -- it
// CALLS it, on a real PedalAudioProcessor. A mirrored copy of the gain staging is a second
// implementation that has to be kept in step with the first, and the failure mode when it drifts
// is the worst kind: the harness reports a mismatch against the reference renders that exists
// only in the harness. Since every calibration constant this project has left to fit is read off
// exactly these measurements, the instrument must be the thing under test, not a likeness of it.
// The cost is that this links the editor and the UI assets, which is build time only.
//
// Unknown flags are a hard error rather than being ignored: a caller passing a flag this binary
// does not implement would otherwise get a render at default settings that looks perfectly valid.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

using namespace juce;

namespace
{
// Choice index order is the APVTS layout's: physical lever position up/middle/down, NOT brightness
// (circuit.md note #2). captures.py's MODE_LABELS holds the same order -- keep the two in step.
const StringArray kModeNames{"bright", "dark", "mid"};

int osFactorToIndex(int factor)
{
    switch (factor)
    {
    case 1:
        return 0;
    case 2:
        return 1;
    case 4:
        return 2;
    case 8:
        return 3;
    default:
        return -1;
    }
}

void setParam(AudioProcessorValueTreeState& apvts, const String& id, float plainValue)
{
    auto* p = apvts.getParameter(id);
    jassert(p != nullptr);
    if (p != nullptr)
        p->setValueNotifyingHost(p->convertTo0to1(plainValue));
}

int fail(const String& msg)
{
    std::cerr << "OfflineRender: " << msg << std::endl;
    return 1;
}
} // namespace

int main(int argc, char* argv[])
{
    const ScopedJuceInitialiser_GUI juceInit;

    StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add(String(argv[i]));

    StringArray positional;
    int osFactor = 8;
    double volume = 0.5, inputTrim = 0.0, outputTrim = 0.0, inputScaleDb = 0.0;
    double vov = 0.0; // 0 = leave the shipped JfetParams::vov alone
    int modeIndex = 1; // Dark -- the APVTS default
    bool bypass = false;
    int blockSize = 512;

    for (int i = 0; i < args.size(); ++i)
    {
        const auto& a = args[i];
        if (! a.startsWith("--"))
        {
            positional.add(a);
            continue;
        }

        if (i + 1 >= args.size())
            return fail("missing value for " + a);
        const auto v = args[++i];

        if (a == "--os")
            osFactor = v.getIntValue();
        else if (a == "--volume")
            volume = v.getDoubleValue();
        else if (a == "--input-trim")
            inputTrim = v.getDoubleValue();
        else if (a == "--input-scale")
            inputScaleDb = v.getDoubleValue();
        else if (a == "--vov")
            vov = v.getDoubleValue();
        else if (a == "--output-trim")
            outputTrim = v.getDoubleValue();
        else if (a == "--bypass")
            bypass = (v.getIntValue() != 0);
        else if (a == "--block")
            blockSize = jmax(1, v.getIntValue());
        else if (a == "--mode")
        {
            // Accept the label the capture filenames carry, or a raw choice index.
            const int byName = kModeNames.indexOf(v.toLowerCase());
            if (byName >= 0)
                modeIndex = byName;
            else if (v.containsOnly("012") && v.isNotEmpty())
                modeIndex = v.getIntValue();
            else
                return fail("unknown --mode '" + v + "' (expected bright|dark|mid or 0|1|2)");
        }
        else
            return fail("unknown flag " + a);
    }

    if (positional.size() != 2)
        return fail("usage: OfflineRender <in.wav> <out.wav> [--os N] [--volume x] [--mode m] ...");

    const int osIndex = osFactorToIndex(osFactor);
    if (osIndex < 0)
        return fail("--os must be 1, 2, 4 or 8 (got " + String(osFactor) + ")");

    const File inFile(File::getCurrentWorkingDirectory().getChildFile(positional[0]));
    const File outFile(File::getCurrentWorkingDirectory().getChildFile(positional[1]));

    AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader(formats.createReaderFor(inFile));
    if (reader == nullptr)
        return fail("cannot read " + inFile.getFullPathName());

    const double sampleRate = reader->sampleRate;
    const int numInCh = (int)reader->numChannels;
    const int numSamples = (int)reader->lengthInSamples;
    if (numSamples <= 0)
        return fail("empty input file");

    // Always run the circuit in stereo (the processor's own layout); a mono source is duplicated so
    // both channels see identical audio, and only channel 0 is written back out.
    AudioBuffer<float> buffer(2, numSamples);
    buffer.clear();
    reader->read(&buffer, 0, numSamples, 0, true, numInCh > 1);
    if (numInCh == 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);

    PedalAudioProcessor proc;

    // Parameters BEFORE prepareToPlay, so the smoothers initialise to their final values and the
    // first block carries no ramp artefact (architecture.md prepareToPlay responsibilities).
    setParam(proc.apvts, "trim_link", 0.0f); // never let the link nudge a trim we just set
    setParam(proc.apvts, "volume", (float)jlimit(0.0, 1.0, volume));
    // ⚠⚠ FAIL, DO NOT CLAMP. These used to be jlimit()ed, and it cost a whole set of reports: the
    // matched-drive offset for P1 is -24.2 dB once kInputRef is 4.4626, which silently became -12
    // and drove every audit 12.2 dB too hot. A trim is a PLUGIN CONTROL with a real range; drive
    // matching is a MEASUREMENT scaling and belongs in --input-scale, which has no range at all.
    if (inputTrim < -12.0 || inputTrim > 12.0)
        return fail("--input-trim " + String(inputTrim) + " dB is outside the parameter's [-12, +12] "
                    "range. Use --input-scale for drive matching -- it scales the SIGNAL and is not "
                    "a plugin control, so it has no range limit.");
    if (outputTrim < -12.0 || outputTrim > 12.0)
        return fail("--output-trim " + String(outputTrim) + " dB is outside the parameter's "
                    "[-12, +12] range.");
    setParam(proc.apvts, "input_trim", (float)inputTrim);
    setParam(proc.apvts, "output_trim", (float)outputTrim);
    setParam(proc.apvts, "bypass", bypass ? 1.0f : 0.0f);

    if (auto* modeParam = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter("mode")))
        modeParam->setValueNotifyingHost(modeParam->convertTo0to1((float)modeIndex));

    // processBlock picks render_oversampling when isNonRealtime() is true and oversampling
    // otherwise. Set BOTH from --os so the factor is what was asked for either way, and the render
    // never depends on which path a future edit takes.
    if (auto* p = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter("oversampling")))
        p->setValueNotifyingHost(p->convertTo0to1((float)osIndex));
    if (auto* p = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter("render_oversampling")))
        p->setValueNotifyingHost(p->convertTo0to1((float)osIndex));

    // --vov overrides the JFET stage's one amplitude parameter. It is a MEASUREMENT flag, not a
    // plugin control: fitting Vov means sweeping it against a calibrated capture, and every other
    // quantity in the device model (Id0, beta, |Vp|, IDSS, the quiescent drain voltage) is derived
    // from it, so it cannot be swept from outside the stage. Set before prepareToPlay so the
    // derived operating point is in place when the first block runs.
    if (vov > 0.0)
        proc.setVov(vov);

    proc.setNonRealtime(true);
    proc.setPlayConfigDetails(2, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    // The oversampler's FIR is linear phase and reports its latency; flush that many extra samples
    // through and drop them off the front so the output is sample-aligned with the input. The
    // harness aligns too, but starting aligned means a non-zero lag out of analyze.align() is a
    // real signal (a bug) rather than the expected latency.
    const int latency = jmax(0, proc.getLatencySamples());
    AudioBuffer<float> work(2, numSamples + latency);
    work.clear();
    for (int ch = 0; ch < 2; ++ch)
        work.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    // --input-scale is applied to the SIGNAL, ahead of the processor, so it is not a plugin control
    // and carries no range limit. This is how a matched-DRIVE comparison is made when the capture
    // rig's volts-per-full-scale differs from kInputRef by more than a trim can express.
    if (inputScaleDb != 0.0)
        work.applyGain(Decibels::decibelsToGain(inputScaleDb));

    MidiBuffer midi;
    for (int start = 0; start < work.getNumSamples(); start += blockSize)
    {
        const int n = jmin(blockSize, work.getNumSamples() - start);
        AudioBuffer<float> sub(work.getArrayOfWritePointers(), 2, start, n);
        midi.clear();
        proc.processBlock(sub, midi);
    }

    const int numOutCh = numInCh == 1 ? 1 : 2;
    AudioBuffer<float> out(numOutCh, numSamples);
    for (int ch = 0; ch < numOutCh; ++ch)
        out.copyFrom(ch, 0, work, ch, latency, numSamples);

    // 32-bit float, deliberately: this pedal's output legitimately exceeds 0 dBFS at high volume
    // settings (a faithful result, not a fault -- CLAUDE.md step 10), and an integer format would
    // clip exactly the renders the level calibration is read from.
    outFile.deleteFile();
    outFile.getParentDirectory().createDirectory();
    std::unique_ptr<OutputStream> stream(outFile.createOutputStream());
    if (stream == nullptr)
        return fail("cannot write " + outFile.getFullPathName());

    WavAudioFormat wav;
    const auto options = AudioFormatWriterOptions{}
                             .withSampleRate(sampleRate)
                             .withNumChannels(numOutCh)
                             .withBitsPerSample(32)
                             .withSampleFormat(AudioFormatWriterOptions::SampleFormat::floatingPoint);
    std::unique_ptr<AudioFormatWriter> writer(wav.createWriterFor(stream, options));
    if (writer == nullptr)
        return fail("cannot create WAV writer");
    stream.release(); // the writer owns it now

    if (! writer->writeFromAudioSampleBuffer(out, 0, numSamples))
        return fail("write failed");
    writer.reset();

    std::cout << "OfflineRender: " << numSamples << " smp @ " << sampleRate << " Hz, " << osFactor << "x OS, latency "
              << latency << " smp compensated, "
              << "volume " << volume << ", mode " << kModeNames[modeIndex] << (bypass ? ", BYPASSED" : "") << std::endl;
    return 0;
}
