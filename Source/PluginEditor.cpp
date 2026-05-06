#include "PluginEditor.h"

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

    repaint();
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------
void HumHouseVocalTuneEditor::paint (juce::Graphics& g)
{
    // Background gradient — deep dark purple
    juce::ColourGradient bgGrad(Palette::bgDark, 0.0f, 0.0f,
                                 juce::Colour(0xff12121e), 0.0f, static_cast<float>(getHeight()),
                                 false);
    g.setGradientFill(bgGrad);
    g.fillAll();

    auto bounds = getLocalBounds();

    // --- Title bar ---
    auto topBar = bounds.removeFromTop(40);
    g.setColour(Palette::bgPanel);
    g.fillRect(topBar);

    g.setColour(Palette::purpleBright);
    g.setFont(juce::Font(juce::FontOptions(20.0f).withStyle("Bold")));
    g.drawText("HumHouse Vocal Tune", topBar.reduced(12, 0), juce::Justification::centredLeft);

    g.setColour(Palette::textDim);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText("v1.0.0", topBar.reduced(12, 0), juce::Justification::centredRight);

    // --- Pitch Orb Visualizer (centre area) ---
    auto orbArea = juce::Rectangle<int>(40, 55, 340, 260);

    // Outer glow circle
    {
        float cx = static_cast<float>(orbArea.getCentreX());
        float cy = static_cast<float>(orbArea.getCentreY());
        float r  = 110.0f;

        // Outer ring
        g.setColour(Palette::purpleGlow);
        g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 2.0f);
        g.drawEllipse(cx - r - 10.0f, cy - r - 10.0f,
                       (r + 10.0f) * 2.0f, (r + 10.0f) * 2.0f, 1.0f);

        // Inner filled orb with gradient
        juce::ColourGradient orbGrad(
            Palette::purpleBright.withAlpha(0.6f), cx - 30.0f, cy - 30.0f,
            Palette::bgDark.withAlpha(0.9f), cx + 60.0f, cy + 60.0f, true);
        g.setGradientFill(orbGrad);
        g.fillEllipse(cx - r * 0.65f, cy - r * 0.65f,
                       r * 1.3f, r * 1.3f);

        // Correction indicator — line from center showing direction
        if (displayDetectedHz > 60.0f && displayConfidence > 0.3f)
        {
            float normCorrection = juce::jlimit(-1.0f, 1.0f,
                                                  displayCorrectionCents / 100.0f);
            float angle = normCorrection * juce::MathConstants<float>::halfPi;
            float indicatorLen = r * 0.5f * displayConfidence;
            float ix = cx + indicatorLen * std::sin(angle);
            float iy = cy - indicatorLen * std::cos(angle);

            g.setColour(Palette::purpleBright);
            g.drawLine(cx, cy, ix, iy, 3.0f);
            g.fillEllipse(ix - 5.0f, iy - 5.0f, 10.0f, 10.0f);
        }

        // Detected note text in orb
        g.setColour(Palette::textBright);
        g.setFont(juce::Font(juce::FontOptions(38.0f).withStyle("Bold")));
        juce::String noteText = HumHouseVocalTuneProcessor::midiNoteToName(displayDetectedMidi);
        if (noteText.isEmpty()) noteText = "--";
        g.drawText(noteText,
                   static_cast<int>(cx - 50.0f), static_cast<int>(cy - 25.0f),
                   100, 50, juce::Justification::centred);
    }

    // --- Heatmap (below orb) ---
    auto heatArea = juce::Rectangle<int>(40, 325, 340, 40);
    g.setColour(Palette::bgSection);
    g.fillRoundedRectangle(heatArea.toFloat(), 4.0f);

    // Draw scrolling pitch lines
    for (int i = 0; i < kHeatmapWidth; ++i)
    {
        int idx = (heatmapWriteIdx + i) % kHeatmapWidth;
        float det = heatmapDetected[static_cast<size_t>(idx)];
        float tgt = heatmapTarget[static_cast<size_t>(idx)];

        if (det < 60.0f) continue;

        float x = static_cast<float>(heatArea.getX())
                 + static_cast<float>(i) / static_cast<float>(kHeatmapWidth)
                 * static_cast<float>(heatArea.getWidth());

        // Map Hz to Y (log scale, 60..1500 Hz)
        auto hzToY = [&](float hz) -> float
        {
            float norm = (std::log2(hz) - std::log2(60.0f))
                       / (std::log2(1500.0f) - std::log2(60.0f));
            norm = juce::jlimit(0.0f, 1.0f, norm);
            return static_cast<float>(heatArea.getBottom())
                 - norm * static_cast<float>(heatArea.getHeight());
        };

        float yDet = hzToY(det);
        g.setColour(Palette::heatCold.withAlpha(0.7f));
        g.fillRect(x, yDet, 2.0f, 2.0f);

        if (tgt > 60.0f)
        {
            float yTgt = hzToY(tgt);
            g.setColour(Palette::heatWarm.withAlpha(0.9f));
            g.fillRect(x, yTgt, 2.0f, 2.0f);
        }
    }

    // Heatmap labels
    g.setColour(Palette::textDim);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("In/Out HeatMap", heatArea.getX(), heatArea.getY() - 14,
               heatArea.getWidth(), 14, juce::Justification::centredRight);

    // --- Input / Output level meters ---
    auto meterArea = juce::Rectangle<int>(40, 375, 340, 20);
    g.setColour(Palette::textDim);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("Input",  meterArea.getX(), meterArea.getY(), 40, 10, juce::Justification::centredLeft);
    g.drawText("Output", meterArea.getX(), meterArea.getY() + 10, 40, 10, juce::Justification::centredLeft);

    auto drawMeter = [&](float level, int y)
    {
        int meterX = meterArea.getX() + 45;
        int meterW = meterArea.getWidth() - 50;
        float barW = juce::jlimit(0.0f, 1.0f, level) * static_cast<float>(meterW);

        g.setColour(Palette::bgSection);
        g.fillRoundedRectangle(static_cast<float>(meterX), static_cast<float>(y),
                                static_cast<float>(meterW), 6.0f, 2.0f);

        juce::Colour mCol = (level > 0.9f) ? Palette::meterRed
                           : (level > 0.6f) ? Palette::meterYellow
                           : Palette::meterGreen;
        g.setColour(mCol);
        g.fillRoundedRectangle(static_cast<float>(meterX), static_cast<float>(y),
                                barW, 6.0f, 2.0f);
    };

    drawMeter(displayInputLevel,  meterArea.getY() + 2);
    drawMeter(displayOutputLevel, meterArea.getY() + 12);

    // --- Detected / Target info ---
    auto infoArea = juce::Rectangle<int>(400, 320, 360, 75);
    g.setColour(Palette::textDim);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));

    juce::String detNote = HumHouseVocalTuneProcessor::midiNoteToName(displayDetectedMidi);
    juce::String tgtNote = HumHouseVocalTuneProcessor::midiNoteToName(displayTargetMidi);
    juce::String corrStr = juce::String(displayCorrectionCents, 1) + " cents";
    juce::String confStr = juce::String(static_cast<int>(displayConfidence * 100.0f)) + "%";

    g.drawText("Detected: " + (detNote.isEmpty() ? "--" : detNote)
               + "  (" + juce::String(displayDetectedHz, 1) + " Hz)",
               infoArea.getX(), infoArea.getY(), infoArea.getWidth(), 18,
               juce::Justification::centredLeft);
    g.drawText("Target:   " + (tgtNote.isEmpty() ? "--" : tgtNote)
               + "  (" + juce::String(displayTargetHz, 1) + " Hz)",
               infoArea.getX(), infoArea.getY() + 18, infoArea.getWidth(), 18,
               juce::Justification::centredLeft);
    g.drawText("Correction: " + corrStr + "   Confidence: " + confStr,
               infoArea.getX(), infoArea.getY() + 36, infoArea.getWidth(), 18,
               juce::Justification::centredLeft);

    // --- Piano keyboard background ---
    auto pianoArea = juce::Rectangle<int>(40, 500, 700, 90);
    g.setColour(Palette::bgPanel);
    g.fillRoundedRectangle(pianoArea.toFloat(), 6.0f);

    g.setColour(Palette::textDim);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText("Scale Notes", pianoArea.getX(), pianoArea.getY() - 16,
               pianoArea.getWidth(), 16, juce::Justification::centredLeft);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
void HumHouseVocalTuneEditor::resized()
{
    // --- Main knobs (right side) ---
    int knobSize = 90;
    int knobX = 440;
    int knobY = 60;

    speedKnob.setBounds(knobX, knobY, knobSize, knobSize);
    speedLabel.setBounds(knobX, knobY + knobSize - 2, knobSize, 16);

    sustainKnob.setBounds(knobX + 120, knobY, knobSize, knobSize);
    sustainLabel.setBounds(knobX + 120, knobY + knobSize - 2, knobSize, 16);

    amountKnob.setBounds(knobX + 60, knobY + 100, knobSize, knobSize);
    amountLabel.setBounds(knobX + 60, knobY + 100 + knobSize - 2, knobSize, 16);

    // Smaller knobs
    int smKnobSize = 65;
    int row2Y = 280;

    humanizeKnob.setBounds(knobX + 180, row2Y, smKnobSize, smKnobSize);
    humanizeLabel.setBounds(knobX + 180, row2Y + smKnobSize - 2, smKnobSize, 16);

    mixKnob.setBounds(knobX + 260, row2Y, smKnobSize, smKnobSize);
    mixLabel.setBounds(knobX + 260, row2Y + smKnobSize - 2, smKnobSize, 16);

    inputGainKnob.setBounds(knobX + 180, row2Y - 90, smKnobSize, smKnobSize);
    inputGainLabel.setBounds(knobX + 180, row2Y - 90 + smKnobSize - 2, smKnobSize, 16);

    outputGainKnob.setBounds(knobX + 260, row2Y - 90, smKnobSize, smKnobSize);
    outputGainLabel.setBounds(knobX + 260, row2Y - 90 + smKnobSize - 2, smKnobSize, 16);

    referenceFreqKnob.setBounds(knobX + 220, row2Y + 80, smKnobSize, smKnobSize);
    referenceFreqLabel.setBounds(knobX + 220, row2Y + 80 + smKnobSize - 2, smKnobSize, 16);

    // --- Scale mode buttons ---
    int btnY = 408;
    int btnW = 80;
    int btnH = 26;
    int btnX = 40;
    majorBtn.setBounds(btnX, btnY, btnW, btnH);
    minorBtn.setBounds(btnX + btnW + 6, btnY, btnW, btnH);
    chromBtn.setBounds(btnX + (btnW + 6) * 2, btnY, btnW + 10, btnH);

    // --- Toggle buttons ---
    int togX = btnX + (btnW + 6) * 3 + 20;
    int togW = 105;
    stabilizerBtn.setBounds(togX, btnY, togW, btnH);
    formantBtn.setBounds(togX + togW + 6, btnY, 80, btnH);
    lowLatBtn.setBounds(togX + togW + 86 + 6, btnY, 90, btnH);

    // --- Enable / bypass ---
    enableBtn.setBounds(getWidth() - 110, 8, 95, 24);

    // --- Root note ---
    rootNoteBox.setBounds(40, 455, 70, 26);
    rootNoteLabel.setBounds(115, 455, 40, 26);

    // --- Piano note buttons ---
    int pianoX = 40;
    int pianoY = 510;
    int whiteW = 54;
    int whiteH = 72;
    int blackW = 36;
    int blackH = 45;

    // Layout: 7 white keys + 5 black keys overlaid
    // C  D  E  F  G  A  B  (white)
    // C# D#    F# G# A#    (black)
    int whiteIdx[] = { 0, 2, 4, 5, 7, 9, 11 };
    int blackIdx[] = { 1, 3, 6, 8, 10 };

    for (int i = 0; i < 7; ++i)
    {
        auto& btn = noteButtons[static_cast<size_t>(whiteIdx[i])];
        btn.setBounds(pianoX + i * (whiteW + 2), pianoY, whiteW, whiteH);
        btn.setColour(juce::TextButton::buttonColourId,
                       btn.getToggleState() ? Palette::keyWhite : Palette::keyDisabled);
        btn.setColour(juce::TextButton::buttonOnColourId, Palette::keyActive);
    }

    // Black keys positioned between whites
    int blackPositions[] = { 0, 1, 3, 4, 5 }; // position relative to white key index
    for (int i = 0; i < 5; ++i)
    {
        auto& btn = noteButtons[static_cast<size_t>(blackIdx[i])];
        int xOff = pianoX + blackPositions[i] * (whiteW + 2) + whiteW - blackW / 2 + 1;
        btn.setBounds(xOff, pianoY, blackW, blackH);
        btn.setColour(juce::TextButton::buttonColourId,
                       btn.getToggleState() ? Palette::keyBlack : Palette::keyDisabled);
        btn.setColour(juce::TextButton::buttonOnColourId, Palette::keyActive);
        btn.toFront(false);
    }
}
