#include "PedalFace.h"

#include <cmath>

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
    // No on-screen text label -- the pedal art itself carries the VOLUME lettering.
    volumeKnob.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    volumeKnob.setRotaryParameters(MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true); // 270°, gap at bottom
    volumeKnob.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);  // value shown as a tooltip, not JUCE's text box
    addAndMakeVisible(volumeKnob);
    sliderAttachments.push_back(std::make_unique<SliderParameterAttachment>(*state.getParameter("volume"), volumeKnob));
    // Update the drag tooltip to the real-world value (2 dp). Needs a TooltipWindow somewhere
    // in the hierarchy — the editor owns one. (Alternatively use s.setPopupDisplayEnabled(...).)
    volumeKnob.onValueChange = [this] { volumeKnob.setTooltip(fmtDial(volumeKnob.getValue())); };
    volumeKnob.setTooltip(fmtDial(volumeKnob.getValue()));

    // ---- 3-position MODE switch, bound to the "mode" AudioParameterChoice -------------------------
    // Two-way binding: the ParameterAttachment drives the switch when the host/automation changes
    // the param; the switch's onChange writes back as a complete gesture. Labelled top-to-bottom by
    // physical lever position (circuit.md: up=Bright, middle=Dark/centre-off, down=Mid), matching
    // the AudioParameterChoice order in PluginProcessor.
    // Labels are placed by hand around the switch art (see resized()) rather than in
    // ThreePositionSwitch's generic side-column, so the switch itself draws image-only.
    modeSwitch.setShowInlineLabels(false);
    addAndMakeVisible(modeSwitch);

    auto setupModeLabel = [this](Label& l, const String& text) {
        l.setText(text, dontSendNotification);
        l.setJustificationType(Justification::centred);
        l.setInterceptsMouseClicks(false, false);  // clicks/drags pass through to the switch beneath
        addAndMakeVisible(l);
    };
    setupModeLabel(modeLabelBright, "BRIGHT");
    setupModeLabel(modeLabelDark, "DARK");
    setupModeLabel(modeLabelMid, "MID");
    // Labels were added after the switch, which would normally paint them ON TOP of it; the DARK
    // label is meant to tuck partly under the switch body, so bring the switch back to front.
    modeSwitch.toFront(false);

    modeAttachment = std::make_unique<ParameterAttachment>(
        *state.getParameter("mode"),
        [this](float v) {
            const int pos = (int) std::lround(v);
            modeSwitch.setPosition(pos);
            updateModeLabelHighlight(pos);
        });
    modeSwitch.onChange = [this](int pos) {
        // ParameterAttachment suppresses its own callback for a change it originated, so update
        // the label highlight here too -- host/automation-driven changes go through the callback
        // passed to the ParameterAttachment above instead.
        updateModeLabelHighlight(pos);
        modeAttachment->setValueAsCompleteGesture((float) pos);
    };
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

namespace
{
// The pedal art (echopre_texture.jpg + this layout) was drawn on a fixed 875x1500 design canvas
// at 250% export scale -- all figures below are AS GIVEN in that canvas (250%); since every
// figure is divided by the same canvas width/height to get a fraction, the 250% cancels out and
// only the ratios matter. (x, y) is each image's CENTRE, per the art brief.
struct ArtSpec { float w, h, cx, cy; };
constexpr ArtSpec kKnobArt       { 328.0f, 328.0f, 215.0f,   260.0f };
constexpr ArtSpec kSwitchArt     { 240.0f, 240.0f, 723.0f,   260.0f };
constexpr ArtSpec kLedArt        { 137.0f, 137.0f, 437.5f,   867.0f };
constexpr ArtSpec kFootswitchArt { 248.0f, 248.0f, 437.0f,  1240.0f };

// Mode labels ring the switch art: BRIGHT above (up position), DARK to the left at the switch's
// vertical centre (mid position -- no room above/below since BRIGHT/MID claim those), MID below
// (down position). Each overlaps into the switch image on its near edge (drawn behind the switch
// art -- see modeSwitch.toFront() in the constructor -- so the overlap is invisible either way).
constexpr float kLabelOverlapTopBot = 15.0f;  // BRIGHT / MID
constexpr float kLabelOverlapSide   = 20.0f;  // DARK -- tucked 5px further under the switch than BRIGHT/MID
constexpr float kLabelH       = 56.0f;   // fits the label font with ascender/descender room
constexpr float kLabelWTop    = 260.0f;  // BRIGHT / MID (centred on the switch's own x)
constexpr float kLabelWSide   = 200.0f;  // DARK (butts against the switch's left edge)
constexpr float kLabelFontPx  = 45.0f;   // 40px art-brief size + 5
} // namespace

void PedalFace::resized()
{
    const auto art = PedalLookAndFeel::fitDesignCanvas(getLocalBounds());
    // Design canvas is fit (not stretched) into the component, so x and y share one scale factor.
    const float s = art.getWidth() / PedalLookAndFeel::kDesignW;
    const float ox = art.getX(), oy = art.getY();

    auto place = [](Component& c, float cx, float cy, float w, float h) {
        c.setBounds(roundToInt(cx - w * 0.5f), roundToInt(cy - h * 0.5f), roundToInt(w), roundToInt(h));
    };
    auto placeArt = [&](Component& c, const ArtSpec& a) {
        place(c, ox + a.cx * s, oy + a.cy * s, a.w * s, a.h * s);
    };

    placeArt(volumeKnob, kKnobArt);
    placeArt(modeSwitch, kSwitchArt);
    placeArt(led, kLedArt);
    placeArt(bypassSwitch, kFootswitchArt);

    // Mode labels, positioned relative to the switch art's own edges (computed above).
    const float swLeft = kSwitchArt.cx - kSwitchArt.w * 0.5f;
    const float swTop = kSwitchArt.cy - kSwitchArt.h * 0.5f;
    const float swBottom = kSwitchArt.cy + kSwitchArt.h * 0.5f;
    place(modeLabelBright, ox + kSwitchArt.cx * s, oy + (swTop + kLabelOverlapTopBot - kLabelH * 0.5f) * s,
          kLabelWTop * s, kLabelH * s);
    place(modeLabelDark, ox + (swLeft + kLabelOverlapSide - kLabelWSide * 0.5f) * s, oy + kSwitchArt.cy * s,
          kLabelWSide * s, kLabelH * s);
    place(modeLabelMid, ox + kSwitchArt.cx * s, oy + (swBottom - kLabelOverlapTopBot + kLabelH * 0.5f) * s,
          kLabelWTop * s, kLabelH * s);

    const auto arial = [&](float px) { return Font(FontOptions("Arial", jmax(6.0f, px * s), Font::bold)); };
    modeLabelBright.setFont(arial(kLabelFontPx));
    modeLabelDark.setFont(arial(kLabelFontPx));
    modeLabelMid.setFont(arial(kLabelFontPx));

    // BYPASS label isn't part of the art brief -- fit it into the remaining canvas space below
    // the footswitch.
    const float labH = jmax(10.0f, 40.0f * s);
    place(bypassLabel, ox + kFootswitchArt.cx * s, oy + (kFootswitchArt.cy + kFootswitchArt.h * 0.5f + 30.0f) * s,
          kFootswitchArt.w * 1.6f * s, labH);
    bypassLabel.setFont(Font(FontOptions(jmax(8.0f, 16.0f * s), Font::bold)).withExtraKerningFactor(0.20f));
}

void PedalFace::refresh(float sc)
{
    scale = sc;
}

void PedalFace::updateModeLabelHighlight(int position)
{
    const auto colourFor = [](bool active) {
        return active ? Colours::white : Colours::white.withAlpha(0.30f);
    };
    // AudioParameterChoice order is Bright(0)/Dark(1)/Mid(2) -- circuit.md note #2 / PluginProcessor.
    modeLabelBright.setColour(Label::textColourId, colourFor(position == 0));
    modeLabelDark.setColour(Label::textColourId, colourFor(position == 1));
    modeLabelMid.setColour(Label::textColourId, colourFor(position == 2));
}

void PedalFace::updateLED()
{
    // LED on = active (not bypassed). Read the param directly so it reflects host recall
    // immediately, before any audio has run (ui.md "Metering & threading").
    const auto* p = state.getRawParameterValue("bypass");
    led.setOn(p == nullptr || p->load() < 0.5f);
}
