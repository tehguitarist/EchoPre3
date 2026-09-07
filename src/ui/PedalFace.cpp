#include "PedalFace.h"

#include <cmath>
#include <functional>

using namespace juce;

namespace
{
// VOLUME's raw 0..1 rotation isn't a linear real-world unit (the taper is deliberately
// non-monotonic — circuit.md "Validation notes" #1), so the tooltip reads out the knob's dial
// position (0-10, as printed on the pedal) rather than a derived dB/percent figure.
String fmtDial(double v01) { return String(v01 * 10.0, 2); }  // "0.00" .. "10.00"
} // namespace

PedalFace::PedalFace(AudioProcessorValueTreeState& apvts) : state(apvts)
{
    // ---- VOLUME knob -----------------------------------------------------------------------------
    auto setupKnob = [this](Slider& s, Label& lab, const String& text, const char* paramId,
                            std::function<String(double)> fmt) {
        s.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters(MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true); // 270°, gap at bottom
        s.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);  // value shown as a tooltip, not JUCE's text box
        addAndMakeVisible(s);
        sliderAttachments.push_back(std::make_unique<SliderParameterAttachment>(*state.getParameter(paramId), s));
        // Update the drag tooltip to the real-world value (2 dp). Needs a TooltipWindow somewhere
        // in the hierarchy — the editor owns one. (Alternatively use s.setPopupDisplayEnabled(...).)
        s.onValueChange = [&s, fmt] { s.setTooltip(fmt(s.getValue())); };
        s.setTooltip(fmt(s.getValue()));

        lab.setText(text, dontSendNotification);
        lab.setJustificationType(Justification::centred);
        lab.setColour(Label::textColourId, Colour(PedalLookAndFeel::cLabelText));
        addAndMakeVisible(lab);
    };
    setupKnob(volumeKnob, volumeLabel, "VOLUME", "volume", fmtDial);

    // ---- 3-position MODE switch, bound to the "mode" AudioParameterChoice -------------------------
    // Two-way binding: the ParameterAttachment drives the switch when the host/automation changes
    // the param; the switch's onChange writes back as a complete gesture. Labelled top-to-bottom by
    // physical lever position (circuit.md: up=Bright, middle=Dark/centre-off, down=Mid), matching
    // the AudioParameterChoice order in PluginProcessor.
    modeSwitch.setLabels("BRIGHT", "DARK", "MID");
    addAndMakeVisible(modeSwitch);
    modeAttachment = std::make_unique<ParameterAttachment>(
        *state.getParameter("mode"),
        [this](float v) { modeSwitch.setPosition((int) std::lround(v)); });
    modeSwitch.onChange = [this](int pos) { modeAttachment->setValueAsCompleteGesture((float) pos); };
    modeAttachment->sendInitialUpdate();

    // ---- Status LED + bypass footswitch --------------------------------------------------------
    addAndMakeVisible(led);

    bypassSwitch.setComponentID("bypass");       // -> PedalLookAndFeel footswitch art / vector
    bypassSwitch.setClickingTogglesState(true);
    addAndMakeVisible(bypassSwitch);
    bypassAttachment = std::make_unique<ButtonParameterAttachment>(*state.getParameter("bypass"), bypassSwitch);

    bypassLabel.setText("BYPASS", dontSendNotification);
    bypassLabel.setJustificationType(Justification::centred);
    bypassLabel.setColour(Label::textColourId, Colour(PedalLookAndFeel::cBypassLabel));
    addAndMakeVisible(bypassLabel);

    // ---- Logo ----------------------------------------------------------------------------------
    logoLabel.setText("ECHO PRE 3", dontSendNotification);
    logoLabel.setJustificationType(Justification::centred);
    logoLabel.setColour(Label::textColourId, Colour(PedalLookAndFeel::cLabelText).withAlpha(0.85f));
    addAndMakeVisible(logoLabel);

    updateLED();
}

void PedalFace::paint(Graphics& g)
{
    // Delegate to the LookAndFeel: texture art if embedded (Assets.h), else the procedural
    // mottled background. Keeps the face's look in one place and consistent with the palette.
    if (auto* plf = dynamic_cast<PedalLookAndFeel*>(&getLookAndFeel()))
        plf->paintPedalBackground(g, getLocalBounds());
    else
        g.fillAll(Colour(PedalLookAndFeel::cPedalFace));
}

void PedalFace::resized()
{
    const float W = (float) getWidth(), H = (float) getHeight();
    const float knobD = jmin(W * 0.20f, H * 0.28f);
    const float labH  = jmax(10.0f, H * 0.06f);

    auto place = [](Component& c, float cx, float cy, float w, float h) {
        c.setBounds(roundToInt(cx - w * 0.5f), roundToInt(cy - h * 0.5f), roundToInt(w), roundToInt(h));
    };
    auto placeLabelUnder = [&](Label& l, float cx, float cyKnob, float knob) {
        place(l, cx, cyKnob + knob * 0.72f, knob * 1.8f, labH);
    };

    // Logo across the top.
    place(logoLabel, W * 0.5f, H * 0.12f, W * 0.9f, jmax(14.0f, H * 0.12f));

    // Knob row: VOLUME is the only pot on this pedal, so it sits centred.
    const float knobY = H * 0.42f;
    const float vx = W * 0.5f;
    place(volumeKnob, vx, knobY, knobD, knobD);
    placeLabelUnder(volumeLabel, vx, knobY, knobD);

    // Bottom row: mode switch (left), LED (centre), footswitch (right).
    // ThreePositionSwitch lays its body + label column out to roughly 1.35x its own height (see
    // its internal `sc` scaling) -- with the real BRIGHT/DARK/MID labels (vs. the template's I/II/
    // III placeholders) a width driven off W alone clips the text, so derive width from height.
    const float bottomY = H * 0.76f;
    const float modeH = H * 0.30f;
    const float modeW = 1.4f * modeH;
    place(modeSwitch, W * 0.20f, bottomY, modeW, modeH);

    const float ledD = jmin(W, H) * 0.07f;
    place(led, W * 0.5f, bottomY - ledD * 0.4f, ledD, ledD);

    const float fsD = jmin(W * 0.20f, H * 0.32f);
    const float fsX = W * 0.78f, fsY = bottomY;
    place(bypassSwitch, fsX, fsY, fsD, fsD);
    place(bypassLabel,  fsX, fsY + fsD * 0.62f, fsD * 1.7f, labH);
}

void PedalFace::refresh(float sc)
{
    scale = sc;
    auto bold = [](float sz) { return Font(FontOptions(jmax(8.0f, sz), Font::bold)); };
    volumeLabel.setFont(bold(11.0f * sc).withExtraKerningFactor(0.10f));
    bypassLabel.setFont(bold(8.0f * sc).withExtraKerningFactor(0.20f));
    logoLabel.setFont(bold(18.0f * sc).withExtraKerningFactor(0.15f));
}

void PedalFace::updateLED()
{
    // LED on = active (not bypassed). Read the param directly so it reflects host recall
    // immediately, before any audio has run (ui.md "Metering & threading").
    const auto* p = state.getRawParameterValue("bypass");
    led.setOn(p == nullptr || p->load() < 0.5f);
}
