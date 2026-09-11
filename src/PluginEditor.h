#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/PedalFace.h"
#include "ui/PedalLookAndFeel.h"
#include "ui/VUMeter.h"

/**
 * Reusable editor shell: the three-column layout from ui.md — INPUT side panel (left), the
 * pedal-specific PedalFace (centre), OUTPUT side panel (right) — with a full-width oversampling /
 * UI-scale strip beneath. Only PedalFace changes per pedal; everything here is shared.
 *
 * Expects this interface on the processor (see architecture.md / docs/ui-peripheral-spec.md):
 *   - `juce::AudioProcessorValueTreeState apvts` with params: input_trim, output_trim,
 *     oversampling, render_oversampling, bypass, volume, mode, and OPTIONALLY
 *     hq + trim_link (guarded below — the strip drops the toggle if the param is absent),
 *   - `float getInputLevel(int channel)` / `float getOutputLevel(int channel)` (post-/pre-trim
 *     peak, DAW domain), backed by std::atomic<float> written on the audio thread.
 *
 * Rename `PedalAudioProcessor` to your processor class if it differs.
 */
class PedalAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PedalAudioProcessorEditor(PedalAudioProcessor&);
    ~PedalAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;         // ~33 fps: VU ballistics + LED
    void refreshFonts(float sc);           // re-set every label font (fonts must scale with the window)
    void showScaleMenu();
    void saveDefaultScale();               // cross-session default via ApplicationProperties
    static float maxScaleForDisplay();     // 2.5x, or less if the screen cannot show it

    // Base (1x) window size, 1.5x the plugin's original base -- every literal pixel constant in
    // resized()/refreshFonts() (panel/knob/VU sizes, OS-strip spacing, font points) is scaled up
    // to match, since changing kBaseW/kBaseH alone would only add empty space at sc=1, not make
    // anything visually bigger (sc = width/kBaseW, so content stays the same absolute size at
    // sc=1 regardless of kBaseW unless the literals move too).
    //
    // The pedal face is the centre; side panels + OS strip are fixed-width in px, and the face
    // column is sized to exactly match the pedal art's own 875:1500 aspect (PedalLookAndFeel::
    // kDesignW/H) against topH -- see resized() -- so there's no leftover letterbox gutter between
    // the face and the side panels. panelW is a bit wider than the side panels' own content
    // strictly needs (the 105px knob + a modest lateral pad), because the OS strip below
    // (LIVE/RENDER selectors, TRIM LINK, version stamp, UI SIZE) has its own fixed-pixel content
    // that must still fit across the full window width -- if kBaseW ever gets clipped, resized()'s
    // OS-strip layout is the thing that's binding, not the pedal face or the side panels. If the
    // art's aspect, the knob/VU sizes, or the OS strip's content ever change, recompute kBaseW as
    // 2*margin + 2*panelW + 2*colGap + topH*(kDesignW/kDesignH), using resized()'s own
    // margin/panelW/colGap/topH constants at sc=1, checked against the OS strip's own minimum
    // content width (its literals summed, plus inset and margin, plus room for the version stamp).
    static constexpr int kBaseW = 642;
    static constexpr int kBaseH = 540;

    PedalAudioProcessor& audioProcessor;
    PedalLookAndFeel     lnf;
    juce::TooltipWindow  tooltipWindow { this };   // enables the knob/trim value tooltips
    juce::ApplicationProperties appProps;
    float currentScale { 1.0f };

    // Debounced cross-session scale save: restarted on every resized(), fires ~500 ms after the
    // last one so a corner-drag coalesces into a single disk write (ui-peripheral-spec.md).
    struct DebouncedSave : juce::Timer {
        std::function<void()> action;
        void timerCallback() override { if (action) action(); stopTimer(); }
    } scaleSaveDebounce;

    // ---- Side panels: Input (left) / Output (right) --------------------------------------------
    juce::Label  inputSectionLabel, outputSectionLabel;
    juce::Slider inputTrim, outputTrim;
    juce::Label  inputTrimSub, outputTrimSub;      // "TRIM"
    juce::Label  inputTrimValue, outputTrimValue;  // always-visible dB readout, e.g. "+3.00 dB"
    VUMeter      inputVU, outputVU;
    std::unique_ptr<juce::SliderParameterAttachment> inputTrimAttach, outputTrimAttach;
    float vuInDecay { 0.0f }, vuOutDecay { 0.0f };

    // ---- Oversampling / scale strip ------------------------------------------------------------
    juce::Label      osLabel, osLiveLabel, osRenderLabel, osSizeLabel, versionLabel;
    juce::Label      loadLabel;
    juce::ComboBox   osRealtimeBox, osRenderBox, loadBox;
    juce::TextButton hqButton, trimLinkButton;     // optional toggles (componentID "os")
    juce::TextButton scaleBtn;                      // componentID "os-selector"
    std::unique_ptr<juce::ComboBoxParameterAttachment> osRealtimeAttach, osRenderAttach, loadAttach;
    std::unique_ptr<juce::ButtonParameterAttachment>   hqAttach, trimLinkAttach;

    std::unique_ptr<PedalFace> pedalFace;
    juce::Rectangle<int> pedalFaceArea;
    juce::Rectangle<int> osStripArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PedalAudioProcessorEditor)
};
