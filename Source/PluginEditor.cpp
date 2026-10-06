#include "PluginEditor.h"
#include "Artwork.h"

using Palette = humtune::Palette;

static const juce::StringArray kNoteNames {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

// Which piano keys are "black"
static constexpr bool isBlackKey (int idx)
{
    return idx == 1 || idx == 3 || idx == 6 || idx == 8 || idx == 10;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
HumHouseVocalTuneEditor::HumHouseVocalTuneEditor (HumHouseVocalTuneProcessor& p)
    : AudioProcessorEditor(p), proc(p)
{
    setLookAndFeel(&lnf);

    // Decode the embedded artwork once (see Artwork.h)
    skullSolidImg = juce::ImageFileFormat::loadFrom(humtune::art::skullSolid,
                                                     static_cast<size_t>(humtune::art::skullSolidSize));
    skullHoloImg  = juce::ImageFileFormat::loadFrom(humtune::art::skullHolo,
                                                     static_cast<size_t>(humtune::art::skullHoloSize));
    armImg        = juce::ImageFileFormat::loadFrom(humtune::art::armHand,
                                                     static_cast<size_t>(humtune::art::armHandSize));

    setSize(780, 620);

    auto& apvts = proc.getAPVTS();

    // --- Knobs ---
    setupKnob(speedKnob,     speedLabel,     "Speed");
    setupKnob(sustainKnob,   sustainLabel,   "Sustain");
    setupKnob(amountKnob,    amountLabel,    "Amount");
    setupKnob(humanizeKnob,  humanizeLabel,  "Humanize");
    setupKnob(mixKnob,       mixLabel,       "Mix");
    setupKnob(inputGainKnob, inputGainLabel, "In Gain");
    setupKnob(outputGainKnob,outputGainLabel,"Out Gain");
    setupKnob(referenceFreqKnob, referenceFreqLabel, "Reference");

    referenceFreqKnob.setTextValueSuffix(" Hz");

    speedAtt     = std::make_unique<SliderAttach>(apvts, "speed",     speedKnob);
    sustainAtt   = std::make_unique<SliderAttach>(apvts, "sustain",   sustainKnob);
    amountAtt    = std::make_unique<SliderAttach>(apvts, "amount",    amountKnob);
    humanizeAtt  = std::make_unique<SliderAttach>(apvts, "humanize",  humanizeKnob);
    mixAtt       = std::make_unique<SliderAttach>(apvts, "mix",       mixKnob);
    inputGainAtt = std::make_unique<SliderAttach>(apvts, "inputGain", inputGainKnob);
    outputGainAtt= std::make_unique<SliderAttach>(apvts, "outputGain",outputGainKnob);
    referenceFreqAtt = std::make_unique<SliderAttach>(apvts, "referenceFreq", referenceFreqKnob);

    // --- Scale buttons ---
    auto makeScaleBtn = [&](juce::TextButton& btn, int scaleIdx)
    {
        btn.setClickingTogglesState(true);
        btn.setRadioGroupId(1001);
        btn.setToggleState(scaleIdx == 0, juce::dontSendNotification);
        btn.onClick = [&, scaleIdx]()
        {
            if (auto* param = apvts.getParameter("scaleType"))
                param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(scaleIdx)));
        };
        addAndMakeVisible(btn);
    };
    makeScaleBtn(majorBtn, 0);
    makeScaleBtn(minorBtn, 1);
    makeScaleBtn(chromBtn, 2);

    // --- Toggle buttons ---
    addAndMakeVisible(stabilizerBtn);
    addAndMakeVisible(formantBtn);
    addAndMakeVisible(lowLatBtn);
    addAndMakeVisible(enableBtn);

    stabilizerAtt = std::make_unique<ButtonAttach>(apvts, "noteStabilizer",  stabilizerBtn);
    formantAtt    = std::make_unique<ButtonAttach>(apvts, "formantPreserve", formantBtn);
    lowLatAtt     = std::make_unique<ButtonAttach>(apvts, "lowLatency",      lowLatBtn);
    enableAtt     = std::make_unique<ButtonAttach>(apvts, "enabled",         enableBtn);

    // --- Root note combo ---
    rootNoteBox.addItemList(kNoteNames, 1);
    rootNoteBox.setSelectedItemIndex(0, juce::dontSendNotification);
    addAndMakeVisible(rootNoteBox);
    rootNoteLabel.setText("Key", juce::dontSendNotification);
    rootNoteLabel.setJustificationType(juce::Justification::centred);
    rootNoteLabel.setColour(juce::Label::textColourId, Palette::textDim);
    addAndMakeVisible(rootNoteLabel);
    rootNoteAtt = std::make_unique<ComboAttach>(apvts, "rootNote", rootNoteBox);

    // --- Piano note buttons ---
    for (int i = 0; i < 12; ++i)
    {
        auto& btn = noteButtons[static_cast<size_t>(i)];
        btn.setButtonText(kNoteNames[i]);
        btn.setClickingTogglesState(true);
        btn.setToggleState(true, juce::dontSendNotification);
        // Tell the look-and-feel to draw this as a piano key (1 = white, 2 = black)
        btn.getProperties().set("pianoKey", isBlackKey(i) ? 2 : 1);
        addAndMakeVisible(btn);

        noteAtts[static_cast<size_t>(i)] =
            std::make_unique<ButtonAttach>(apvts, "note" + juce::String(i), btn);
    }

    heatmapDetected.fill(0.0f);
    heatmapTarget.fill(0.0f);

    startTimerHz(30);
}

HumHouseVocalTuneEditor::~HumHouseVocalTuneEditor()
{
    setLookAndFeel(nullptr);
}

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------
void HumHouseVocalTuneEditor::setupKnob (juce::Slider& knob, juce::Label& label,
                                           const juce::String& text)
{
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(knob);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, Palette::textDim);
    label.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(label);
}

// ---------------------------------------------------------------------------
// Artwork: make copies at the exact size they are drawn (done once, not every frame)
// ---------------------------------------------------------------------------
void HumHouseVocalTuneEditor::rebuildScaledArt()
{
    auto scaleTo = [](const juce::Image& src, int w, int h) -> juce::Image
    {
        if (! src.isValid() || w <= 0 || h <= 0)
            return {};
        return src.rescaled(w, h, juce::Graphics::highResamplingQuality);
    };

    // Skull + crossbones, drawn in the middle of the radar
    const int skullW = 170;
    if (skullSolidImg.isValid())
    {
        const int skullH = juce::roundToInt(static_cast<float>(skullW)
                              * static_cast<float>(skullSolidImg.getHeight())
                              / static_cast<float>(skullSolidImg.getWidth()));
        skullSolidScaled = scaleTo(skullSolidImg, skullW, skullH);
        skullHoloScaled  = scaleTo(skullHoloImg,  skullW, skullH);
    }

    // Arm + hand: one copy, and a mirrored copy for the right-hand side
    const int armH = 270;
    if (armImg.isValid())
    {
        const int armW = juce::roundToInt(static_cast<float>(armH)
                              * static_cast<float>(armImg.getWidth())
                              / static_cast<float>(armImg.getHeight()));
        armScaled = scaleTo(armImg, armW, armH);

        armScaledFlipped = juce::Image(juce::Image::ARGB, armW, armH, true);
        juce::Graphics fg(armScaledFlipped);
        fg.addTransform(juce::AffineTransform::scale(-1.0f, 1.0f).translated(static_cast<float>(armW), 0.0f));
        fg.drawImageAt(armScaled, 0, 0);
    }
}

// ---------------------------------------------------------------------------
// Timer — refresh display values
// ---------------------------------------------------------------------------
void HumHouseVocalTuneEditor::timerCallback()
{
    displayDetectedHz      = proc.getDetectedHz();
    displayTargetHz        = proc.getTargetHz();
    displayCorrectionCents = proc.getCorrectionCents();
    displayConfidence      = proc.getConfidence();
    displayDetectedMidi    = proc.getDetectedMidiNote();
    displayTargetMidi      = proc.getTargetMidiNote();
    displayInputLevel      = proc.getInputLevel();
    displayOutputLevel     = proc.getOutputLevel();

    // Update heatmap
    heatmapDetected[static_cast<size_t>(heatmapWriteIdx)] = displayDetectedHz;
    heatmapTarget[static_cast<size_t>(heatmapWriteIdx)]   = displayTargetHz;
    heatmapWriteIdx = (heatmapWriteIdx + 1) % kHeatmapWidth;

    // Update scale buttons from parameter
    int scaleVal = static_cast<int>(*proc.getAPVTS().getRawParameterValue("scaleType"));
    majorBtn.setToggleState(scaleVal == 0, juce::dontSendNotification);
    minorBtn.setToggleState(scaleVal == 1, juce::dontSendNotification);
    chromBtn.setToggleState(scaleVal == 2, juce::dontSendNotification);

    // The skull lights up with the singer's level (fast attack, quick release)
    const float glowTarget = juce::jlimit(0.0f, 1.0f, displayInputLevel * 1.6f);
    skullGlow += 0.35f * (glowTarget - skullGlow);

    repaint();
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------
void HumHouseVocalTuneEditor::paint (juce::Graphics& g)
{
    const float twoPi = juce::MathConstants<float>::twoPi;

    // Background gradient
    juce::ColourGradient bgGrad(Palette::bgDark, 0.0f, 0.0f,
                                 Palette::bgDeep, 0.0f, static_cast<float>(getHeight()),
                                 false);
    g.setGradientFill(bgGrad);
    g.fillAll();

    // Outer frame
    g.setColour(Palette::accentDim);
    g.drawRect(getLocalBounds(), 2);
    g.setColour(Palette::accent.withAlpha(0.*

