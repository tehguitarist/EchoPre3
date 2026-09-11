#include "PluginEditor.h"

#include <cmath>

// JUCE defines this from project(<Pedal> VERSION ...) in a plugin build; the fallback keeps
// non-plugin TUs (UI snapshot exes) compiling. "v" JucePlugin_VersionString is plain adjacent
// string-literal concatenation — no runtime String building.
#ifndef JucePlugin_VersionString
 #define JucePlugin_VersionString "dev"
#endif

using namespace juce;

namespace
{
using pedal::params::kOsChoices;
using pedal::params::kLoadChoices;

constexpr float kScales[] = { 0.50f, 0.75f, 1.00f, 1.25f, 1.50f, 1.75f, 2.00f, 2.25f, 2.50f };
constexpr const char* kScaleLabels[] = { "50%", "75%", "100%", "125%", "150%",
                                         "175%", "200%", "225%", "250%" };

// Trim readout / tooltip: always signed, two decimals, dB unit — "+3.00 dB", "-12.00 dB", "+0.00 dB".
String fmtTrim(double db) { return (db >= 0.0 ? "+" : "") + String(db, 2) + " dB"; }
} // namespace

PedalAudioProcessorEditor::PedalAudioProcessorEditor(PedalAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lnf);

    // Cross-session default scale via ApplicationProperties; per-session via APVTS state.
    PropertiesFile::Options opts;
    // ⚠ These were left as the template's "<Pedal>"/"<You>" placeholders, so the settings file
    // landed at ~/Library/Application Support/<You>/<Pedal>.settings -- literal angle brackets.
    // Matches the convention this author's other plugins already use on disk
    // (~/Library/Application Support/LeighPierce/<Product>.settings), no spaces.
    opts.applicationName     = "EchoPre3";
    opts.filenameSuffix      = ".settings";
    opts.folderName          = "LeighPierce";
    opts.osxLibrarySubFolder = "Application Support";
    appProps.setStorageParameters(opts);

    if (const auto v = audioProcessor.apvts.state.getProperty("uiScale"); ! v.isVoid())
        currentScale = (float) (double) v;
    else
        currentScale = (float) appProps.getUserSettings()->getDoubleValue("defaultScale", 1.0);
    // ⚠⚠ CLAMPED TO WHAT THE DISPLAY CAN SHOW, not just to [0.5, 2.5]. A restored scale larger than
    // the screen puts the resize corner AND the UI-SIZE button off-screen, at which point there is
    // no in-plugin way back -- the only fix is editing the settings file by hand. It happened: a
    // stored 2.249 is 1444 x 1215 logical, taller than the usable height of a 16-inch laptop
    // display. maxScaleForDisplay() is what makes that unreachable rather than merely unlikely.
    currentScale = jlimit(0.5f, maxScaleForDisplay(), currentScale);

    // ---- Side panels ---------------------------------------------------------------------------
    auto setupSectionLabel = [this](Label& l, const String& text) {
        l.setText(text, dontSendNotification);
        l.setJustificationType(Justification::centred);
        l.setColour(Label::textColourId, Colour(PedalLookAndFeel::cTrimLabel));
        addAndMakeVisible(l);
    };
    setupSectionLabel(inputSectionLabel, "INPUT");
    setupSectionLabel(outputSectionLabel, "OUTPUT");

    auto setupTrimSub = [this](Label& l) {
        l.setText("TRIM", dontSendNotification);
        l.setJustificationType(Justification::centred);
        l.setColour(Label::textColourId, Colour(PedalLookAndFeel::cTrimLabel).darker(0.25f));
        addAndMakeVisible(l);
    };
    setupTrimSub(inputTrimSub);
    setupTrimSub(outputTrimSub);

    auto setupTrimValue = [this](Label& l) {
        l.setJustificationType(Justification::centred);
        l.setColour(Label::textColourId, Colour(PedalLookAndFeel::cTrimLabel));
        addAndMakeVisible(l);
    };
    setupTrimValue(inputTrimValue);
    setupTrimValue(outputTrimValue);

    const float pi = MathConstants<float>::pi;
    auto setupTrim = [this, pi](Slider& s) {
        s.setComponentID("trim");                                    // -> halo style in the LookAndFeel
        s.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters(pi * 1.25f, pi * 2.75f, true);         // 270° sweep, gap at bottom
        s.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        s.setDoubleClickReturnValue(true, 0.0);                      // double-click -> 0 dB
        addAndMakeVisible(s);
    };
    setupTrim(inputTrim);
    setupTrim(outputTrim);
    inputTrimAttach  = std::make_unique<SliderParameterAttachment>(*audioProcessor.apvts.getParameter("input_trim"), inputTrim);
    outputTrimAttach = std::make_unique<SliderParameterAttachment>(*audioProcessor.apvts.getParameter("output_trim"), outputTrim);

    // Value label + drag tooltip, both to two decimals with sign, updated together (ui.md Tooltips).
    auto bindTrimReadout = [](Slider& s, Label& valueLabel) {
        auto update = [&s, &valueLabel] {
            const auto txt = fmtTrim(s.getValue());
            s.setTooltip(txt);
            valueLabel.setText(txt, dontSendNotification);
        };
        update();
        s.onValueChange = update;
    };
    bindTrimReadout(inputTrim, inputTrimValue);
    bindTrimReadout(outputTrim, outputTrimValue);

    addAndMakeVisible(inputVU);
    addAndMakeVisible(outputVU);

    // ---- Oversampling / scale strip ------------------------------------------------------------
    auto setupOSLabel = [this](Label& l, const String& text, Justification j) {
        l.setText(text, dontSendNotification);
        l.setJustificationType(j);
        l.setColour(Label::textColourId, Colour(PedalLookAndFeel::cOSLabel));
        addAndMakeVisible(l);
    };
    setupOSLabel(osLabel, "OS", Justification::centredLeft);
    setupOSLabel(osLiveLabel, "LIVE", Justification::centredRight);
    setupOSLabel(osRenderLabel, "RENDER", Justification::centredRight);
    setupOSLabel(osSizeLabel, "UI SIZE", Justification::centredRight);
    setupOSLabel(loadLabel, "LOAD", Justification::centredRight);

    // Self-updating version stamp — from JucePlugin_VersionString (= CMake project VERSION). Muted,
    // non-interactive; given the leftover strip space in resized().
    versionLabel.setText("v" JucePlugin_VersionString, dontSendNotification);
    versionLabel.setJustificationType(Justification::centred);
    versionLabel.setColour(Label::textColourId, Colour(PedalLookAndFeel::cOSLabel).withAlpha(0.55f));
    versionLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(versionLabel);

    auto setupOSBox = [this](ComboBox& box) {
        box.addItemList(kOsChoices, 1);
        box.setJustificationType(Justification::centred);
        box.setColour(ComboBox::textColourId, Colour(PedalLookAndFeel::cOSBtnActive));
        addAndMakeVisible(box);
    };
    setupOSBox(osRealtimeBox);
    setupOSBox(osRenderBox);
    osRealtimeAttach = std::make_unique<ComboBoxParameterAttachment>(*audioProcessor.apvts.getParameter("oversampling"), osRealtimeBox);
    osRenderAttach   = std::make_unique<ComboBoxParameterAttachment>(*audioProcessor.apvts.getParameter("render_oversampling"), osRenderBox);

    // OUTPUT LOAD. In the OS strip rather than on the pedal face because it is not a pedal control
    // -- the hardware has no such knob. It says what the plugin is pretending to drive, which on
    // this circuit is worth about 10 dB of boost (its output impedance is ~95 kOhm, so a load
    // actually loads it). Styled exactly like the OS combo boxes, per ui.md's parity rule.
    if (auto* loadParam = audioProcessor.apvts.getParameter("output_load"))
    {
        setupOSBox(loadBox);
        loadBox.clear(dontSendNotification);
        loadBox.addItemList(kLoadChoices, 1);
        loadBox.setTooltip("What the pedal is driving. Its output impedance is ~95 kOhm, so this is "
                           "worth ~8 dB of boost: 68k is a typical amp front end (and the maker's "
                           "published +3 dB), 1M a modern amp or line input, None open-circuit.");
        loadAttach = std::make_unique<ComboBoxParameterAttachment>(*loadParam, loadBox);
    }

    // Optional quality/behaviour toggles — only shown if the pedal actually declares the param
    // (HQ: dsp.md "HQ / Eco mode"; Trim Link: architecture.md "Input/output trim link", whose
    // mirror logic lives in the PROCESSOR — this button only flips the trim_link bool).
    auto setupToggle = [this](TextButton& b, const String& text, const char* paramId,
                              std::unique_ptr<ButtonParameterAttachment>& attach, const String& tip) {
        if (auto* param = audioProcessor.apvts.getParameter(paramId))
        {
            b.setComponentID("os");            // lit-on / dim-off segmented style
            b.setClickingTogglesState(true);
            b.setButtonText(text);
            b.setTooltip(tip);
            addAndMakeVisible(b);
            attach = std::make_unique<ButtonParameterAttachment>(*param, b);
        }
        // else: never made visible — resized() skips any button that isn't visible.
    };
    setupToggle(hqButton, "HQ", "hq", hqAttach,
                "High-quality diode solve (accurate omega). Off = faster, small added distortion floor.");
    setupToggle(trimLinkButton, "TRIM LINK", "trim_link", trimLinkAttach,
                "Link trims: raising input trim lowers output trim by the same dB, so pushing the "
                "circuit harder doesn't change overall loudness.");

    scaleBtn.setComponentID("os-selector");    // styled like the OS combo boxes (spec parity)
    scaleBtn.onClick = [this] { showScaleMenu(); };
    addAndMakeVisible(scaleBtn);

    pedalFace = std::make_unique<PedalFace>(audioProcessor.apvts);
    addAndMakeVisible(*pedalFace);

    setResizable(true, true);
    if (auto* c = getConstrainer())
    {
        const float maxSc = maxScaleForDisplay();
        c->setFixedAspectRatio((double) kBaseW / (double) kBaseH);
        c->setSizeLimits(roundToInt(kBaseW * 0.5f), roundToInt(kBaseH * 0.5f),
                         roundToInt(kBaseW * maxSc), roundToInt(kBaseH * maxSc));
    }
    setSize(roundToInt(kBaseW * currentScale), roundToInt(kBaseH * currentScale));

    scaleSaveDebounce.action = [this] { saveDefaultScale(); };
    startTimerHz(33);
}

/** The largest UI scale this display can actually show, capped at the design maximum of 2.5x.
 *
 *  ⭐ Leaves 6 % of the usable area as headroom for the host's own window chrome (a plugin window
 *  has a title bar, and some hosts wrap it in a further frame), because a window sized to exactly
 *  the user area still ends up with its bottom edge under the dock or off-screen. Falls back to the
 *  design maximum if JUCE cannot report a display, which is the right way round: a too-large limit
 *  on a headless/unknown display is recoverable, and refusing to resize at all is not.
 */
float PedalAudioProcessorEditor::maxScaleForDisplay()
{
    constexpr float kDesignMax = 2.5f;
    if (auto* d = Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = d->userBounds;
        if (area.getWidth() > 0 && area.getHeight() > 0)
        {
            const float fit = jmin((float) area.getWidth() / (float) kBaseW,
                                   (float) area.getHeight() / (float) kBaseH) * 0.94f;
            return jlimit(0.5f, kDesignMax, fit);
        }
    }
    return kDesignMax;
}

PedalAudioProcessorEditor::~PedalAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void PedalAudioProcessorEditor::paint(Graphics& g)
{
    g.fillAll(Colour(PedalLookAndFeel::cBackground));

    // (PedalFace paints its own body over pedalFaceArea.)

    // Oversampling strip background.
    g.setColour(Colour(PedalLookAndFeel::cOSBackground));
    g.fillRoundedRectangle(osStripArea.toFloat(), 6.0f);
    g.setColour(Colour(PedalLookAndFeel::cOSBorder));
    g.drawRoundedRectangle(osStripArea.toFloat().reduced(0.5f), 6.0f, 1.0f);
}

void PedalAudioProcessorEditor::refreshFonts(float sc)
{
    auto bold = [](float sz) { return Font(FontOptions(sz, Font::bold)); };
    inputSectionLabel.setFont(bold(12.0f * sc).withExtraKerningFactor(0.20f));
    outputSectionLabel.setFont(bold(12.0f * sc).withExtraKerningFactor(0.20f));
    inputTrimSub.setFont(bold(11.3f * sc).withExtraKerningFactor(0.15f));
    outputTrimSub.setFont(bold(11.3f * sc).withExtraKerningFactor(0.15f));
    inputTrimValue.setFont(bold(12.8f * sc));
    outputTrimValue.setFont(bold(12.8f * sc));
    osLabel.setFont(bold(12.0f * sc));
    osLiveLabel.setFont(bold(10.5f * sc).withExtraKerningFactor(0.10f));
    osRenderLabel.setFont(bold(10.5f * sc).withExtraKerningFactor(0.10f));
    loadLabel.setFont(bold(10.5f * sc).withExtraKerningFactor(0.10f));
    osSizeLabel.setFont(bold(10.5f * sc).withExtraKerningFactor(0.10f));
    versionLabel.setFont(Font(FontOptions(10.5f * sc, Font::plain)).withExtraKerningFactor(0.10f));
}

void PedalAudioProcessorEditor::resized()
{
    currentScale = (float) getWidth() / (float) kBaseW;
    const float sc = currentScale;
    const auto i = [sc](int v) { return roundToInt((float) v * sc); };
    refreshFonts(sc);

    const int W = getWidth(), H = getHeight();
    const int margin = i(15);
    const int panelW = i(160);  // wide enough that panelW*2+colGap*2+pedalW+margin*2 clears the OS strip's own fixed-content minimum -- see kBaseW comment
    const int osH    = i(36);
    const int faceGap = i(15);
    const int colGap  = i(12);

    const int topY = margin;
    const int topH = H - margin - osH - faceGap - margin;

    osStripArea = Rectangle<int>(margin, H - margin - osH, W - 2 * margin, osH);

    // The pedal art is a fixed 875:1500 rectangle -- size the centre column to exactly that
    // aspect against topH (kBaseW is tuned so this leaves ~zero slack at sc=1) and butt the
    // output panel directly against it, so any rounding slack lands as extra margin at the
    // window's right edge rather than as a gutter between the face and either side panel.
    const int pedalW = roundToInt((float) topH * (PedalLookAndFeel::kDesignW / PedalLookAndFeel::kDesignH));
    const Rectangle<int> inPanel(margin, topY, panelW, topH);
    pedalFaceArea = Rectangle<int>(margin + panelW + colGap, topY, pedalW, topH);
    const Rectangle<int> outPanel(pedalFaceArea.getRight() + colGap, topY, panelW, topH);
    if (pedalFace != nullptr)
    {
        pedalFace->setBounds(pedalFaceArea);
        pedalFace->refresh(sc);
    }

    // Each element is laid out against its own panel column Rectangle (never the full editor) so
    // the halo knob + VU can't spill past the column edge at any scale (ui.md Layout contract).
    auto layoutPanel = [&](Rectangle<int> panel, Label& sec, Slider& knob, Label& sub, Label& val, VUMeter& vu) {
        auto r = panel;
        sec.setBounds(r.removeFromTop(i(21)));
        r.removeFromTop(i(3));
        const int knobD = jmin(i(105), r.getWidth());
        auto knobRow = r.removeFromTop(knobD);
        knob.setBounds(knobRow.withSizeKeepingCentre(knobD, knobD));
        sub.setBounds(r.removeFromTop(i(18)));
        val.setBounds(r.removeFromTop(i(18)));
        r.removeFromTop(i(3));
        const int vuW = jmin(i(51), r.getWidth());
        vu.setBounds(r.withSizeKeepingCentre(vuW, r.getHeight()));
    };
    layoutPanel(inPanel, inputSectionLabel, inputTrim, inputTrimSub, inputTrimValue, inputVU);
    layoutPanel(outPanel, outputSectionLabel, outputTrim, outputTrimSub, outputTrimValue, outputVU);

    // ---- Oversampling strip content (inset from the bg) ----------------------------------------
    auto os = osStripArea.reduced(i(9), 0);
    const int boxVPad = i(3);

    // Left group: OS | LIVE [box] | RENDER [box] | HQ? | TRIM LINK? | LOAD [box]
    //
    // ⚠ THE HORIZONTAL BUDGET IS TIGHT AND THESE WIDTHS ARE TUNED, NOT DECORATIVE. Available at
    // sc = 1 is kBaseW - 2*margin - 2*inset = 642 - 30 - 18 = 594 px, and the two groups plus the
    // centred version stamp all have to live inside it. The original widths were generous -- 54 px
    // of box for two characters of "4x" -- and adding LOAD at the same generosity overran by 62 px,
    // which silently squeezed the UI SIZE label to nothing (the scale button still reads "250%", so
    // it looked plausible rather than broken). Tightened to fit with ~55 px left for the version.
    // ➡ If another control is ever added here, re-do this arithmetic rather than appending to it,
    // and check the headless UISnapshot render at 0.5x as well as 2.5x -- rounding at the small end
    // is what clips text first.
    osLabel.setBounds(os.removeFromLeft(i(22)));
    os.removeFromLeft(i(10));
    osLiveLabel.setBounds(os.removeFromLeft(i(32)));
    os.removeFromLeft(i(6));
    osRealtimeBox.setBounds(os.removeFromLeft(i(46)).reduced(0, boxVPad));
    os.removeFromLeft(i(14));
    osRenderLabel.setBounds(os.removeFromLeft(i(50)));
    os.removeFromLeft(i(6));
    osRenderBox.setBounds(os.removeFromLeft(i(46)).reduced(0, boxVPad));
    if (hqButton.isVisible())
    {
        os.removeFromLeft(i(12));
        hqButton.setBounds(os.removeFromLeft(i(34)).reduced(0, boxVPad));
    }
    if (trimLinkButton.isVisible())
    {
        os.removeFromLeft(i(10));
        trimLinkButton.setBounds(os.removeFromLeft(i(78)).reduced(0, boxVPad));
    }
    if (loadBox.isVisible())
    {
        os.removeFromLeft(i(14));
        loadLabel.setBounds(os.removeFromLeft(i(34)));
        os.removeFromLeft(i(6));
        loadBox.setBounds(os.removeFromLeft(i(52)).reduced(0, boxVPad));
    }

    // Right group: UI SIZE [scale] — laid out from the right.
    scaleBtn.setBounds(os.removeFromRight(i(62)).reduced(0, boxVPad));
    os.removeFromRight(i(7));
    osSizeLabel.setBounds(os.removeFromRight(i(56)));

    // Version fills whatever is left between the two groups (centred).
    versionLabel.setBounds(os);

    scaleBtn.setButtonText(String(roundToInt(currentScale * 100.0f)) + "%");

    // Persistence — both driven from resized() so a corner-drag is remembered like a menu pick:
    audioProcessor.apvts.state.setProperty("uiScale", (double) currentScale, nullptr); // per-session
    scaleSaveDebounce.startTimer(500);                                                  // cross-session (debounced)
}

void PedalAudioProcessorEditor::timerCallback()
{
    constexpr float kNoiseFl = 5.0e-4f; // -66 dBFS silence floor

    float in = jmax(audioProcessor.getInputLevel(0), vuInDecay * 0.90f);
    if (in < kNoiseFl) in = 0.0f;
    vuInDecay = in;
    inputVU.setLevel(in);

    float out = jmax(audioProcessor.getOutputLevel(0), vuOutDecay * 0.90f);
    if (out < kNoiseFl) out = 0.0f;
    vuOutDecay = out;
    outputVU.setLevel(out);

    if (pedalFace != nullptr)
        pedalFace->updateLED();
}

void PedalAudioProcessorEditor::showScaleMenu()
{
    // ⚠ Presets the display cannot show are DISABLED rather than silently clamped by the
    // constrainer -- picking "250%" and getting 194% reads as a bug, and offering a size that would
    // put this very button off-screen is how a user gets locked out of resizing at all. See
    // maxScaleForDisplay().
    const float maxSc = maxScaleForDisplay();
    PopupMenu menu;
    for (int n = 0; n < 9; ++n)
        menu.addItem(n + 1, kScaleLabels[n], kScales[n] <= maxSc + 0.001f,
                     std::abs(currentScale - kScales[n]) < 0.01f);
    menu.addSeparator();
    menu.addItem(100, "Set current scale as default");

    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&scaleBtn), [this](int r) {
        if (r >= 1 && r <= 9)
            setSize(roundToInt(kBaseW * kScales[r - 1]), roundToInt(kBaseH * kScales[r - 1]));
        else if (r == 100)
            saveDefaultScale();   // immediate (no debounce) for the explicit menu action
    });
}

void PedalAudioProcessorEditor::saveDefaultScale()
{
    appProps.getUserSettings()->setValue("defaultScale", (double) currentScale);
}
