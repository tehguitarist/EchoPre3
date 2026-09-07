#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "LEDIndicator.h"
#include "PedalLookAndFeel.h"
#include "ThreePositionSwitch.h"

/**
 * The centre pedal face — the ONE part of the UI that is unique per pedal. Everything around it
 * (side-panel trims + VU, oversampling strip) is the reusable peripheral kit laid out by
 * PluginEditor; the face just fills the middle column.
 *
 * Echo Pre 3's actual front panel (circuit.md): one pot knob (VOLUME), one 3-position ON-OFF-ON
 * MODE switch (source-bypass network), a status LED and a bypass footswitch, plus the logo. There
 * is no GAIN or TONE knob on this pedal -- the JFET gain stage isn't user-controllable, and MODE is
 * the only EQ-shaping control. For a dual-channel / dual-footswitch pedal, instantiate this class
 * (or a copy) once per stage — see architecture.md "Multiple full gain stages in series".
 *
 * Demonstrates the patterns the template's ui.md asks for:
 *  - knobs set to RotaryHorizontalVerticalDrag with NoTextBox, drawn by PedalLookAndFeel,
 *  - a value tooltip formatted to two decimal places (in the control's real-world unit) while dragging,
 *  - a 3-position switch bound to an AudioParameterChoice (param <-> switch, with gestures),
 *  - a bypass footswitch whose art is momentary (the LED, not the switch, shows bypass state).
 *
 * Binds to the canonical APVTS IDs: "volume", "mode", "bypass".
 */
class PedalFace : public juce::Component
{
public:
    explicit PedalFace(juce::AudioProcessorValueTreeState& apvts);

    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh(float scale);  // rescale fonts/labels (called from the editor's resized())
    void updateLED();           // read the bypass param -> LED on/off (called from the editor timer)

private:
    juce::AudioProcessorValueTreeState& state;
    float scale { 1.0f };

    juce::Slider volumeKnob;  // no on-screen text label -- the pedal art carries the VOLUME lettering

    ThreePositionSwitch modeSwitch;
    juce::Label         modeLabelBright, modeLabelDark, modeLabelMid;  // custom placement -- see resized()
    LEDIndicator        led;
    juce::TextButton    bypassSwitch;
    juce::Label         bypassLabel;

    void updateModeLabelHighlight(int position);

    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::unique_ptr<juce::ParameterAttachment>      modeAttachment;   // param -> switch position
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PedalFace)
};
