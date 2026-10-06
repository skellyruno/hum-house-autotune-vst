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
    g.setColour(Palette::accent.withAlpha(0.35f));
    g.drawRect(getLocalBounds().reduced(3), 1);

    // --- Title bar ---
    auto topBar = juce::Rectangle<int>(0, 0, getWidth(), 44);
    g.setColour(Palette::bgPanel);
    g.fillRect(topBar);
    g.setColour(Palette::accentDim);
    g.drawLine(0.0f, 44.0f, static_cast<float>(getWidth()), 44.0f, 1.5f);

    // Logo ring
    {
        auto logo = juce::Rectangle<float>(12.0f, 6.0f, 32.0f, 32.0f);
        g.setColour(Palette::accentGlow);
        g.drawEllipse(logo.expanded(2.0f), 3.0f);
        g.setColour(Palette::accentBright);
        g.drawEllipse(logo, 1.5f);
        g.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
        g.drawText("HH", logo.toNearestInt(), juce::Justification::centred, false);
    }

    // Title with glow
    {
        auto titleArea = topBar.withTrimmedLeft(60).withTrimmedRight(130);
        const juce::String title = "HumHouse Vocal Tune";
        g.setFont(juce::Font(juce::FontOptions(24.0f).withStyle("Bold")));

        g.setColour(Palette::accent.withAlpha(0.12f));
        for (int dx = -2; dx <= 2; ++dx)
            for (int dy = -2; dy <= 2; ++dy)
                if (dx != 0 || dy != 0)
                    g.drawText(title, titleArea.translated(dx, dy), juce::Justification::centred, false);

        g.setColour(Palette::accentBright);
        g.drawText(title, titleArea, juce::Justification::centred, false);
    }

    g.setColour(Palette::textDim);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText("v1.0.0", juce::Rectangle<int>(getWidth() - 190, 0, 70, 44),
               juce::Justification::centredRight, false);

    // --- Main panel + side panels ---
    auto mainPanel = juce::Rectangle<float>(8.0f, 52.0f, 764.0f, 310.0f);
    g.setColour(Palette::bgPanel.withAlpha(0.9f));
    g.fillRoundedRectangle(mainPanel, 8.0f);
    g.setColour(Palette::accentDim);
    g.drawRoundedRectangle(mainPanel, 8.0f, 1.5f);

    // Left knob column
    g.setColour(Palette::bgSection.withAlpha(0.8f));
    g.fillRoundedRectangle(11.0f, 56.0f, 66.0f, 298.0f, 6.0f);
    g.setColour(Palette::accentDeep);
    g.drawRoundedRectangle(11.0f, 56.0f, 66.0f, 298.0f, 6.0f, 1.0f);

    // Right knob panel
    g.setColour(Palette::bgSection.withAlpha(0.6f));
    g.fillRoundedRectangle(488.0f, 56.0f, 280.0f, 298.0f, 10.0f);
    g.setColour(Palette::accentDeep);
    g.drawRoundedRectangle(488.0f, 56.0f, 280.0f, 298.0f, 10.0f, 1.0f);

    // Corner brackets on the main panel
    {
        g.setColour(Palette::accentBright);
        const float L = 14.0f;
        const float x0 = 8.0f, y0 = 52.0f, x1 = 772.0f, y1 = 362.0f;
        g.drawLine(x0, y0 + L, x0, y0, 2.0f);   g.drawLine(x0, y0, x0 + L, y0, 2.0f);
        g.drawLine(x1 - L, y0, x1, y0, 2.0f);   g.drawLine(x1, y0, x1, y0 + L, 2.0f);
        g.drawLine(x0, y1 - L, x0, y1, 2.0f);   g.drawLine(x0, y1, x0 + L, y1, 2.0f);
        g.drawLine(x1 - L, y1, x1, y1, 2.0f);   g.drawLine(x1, y1 - L, x1, y1, 2.0f);
    }

    // --- Skeleton arms flanking the radar ---
    if (armScaled.isValid())
    {
        const int armY  = 62;
        const int armCx = 282;
        const int dist  = 156;
        g.setOpacity(0.9f);
        g.drawImageAt(armScaled,        armCx - dist - armScaled.getWidth() / 2, armY);
        g.drawImageAt(armScaledFlipped, armCx + dist - armScaled.getWidth() / 2, armY);
        g.setOpacity(1.0f);
    }

    // --- Radar-style pitch visualizer ---
    {
        const float cx = 282.0f;
        const float cy = 206.0f;
        const float R  = 128.0f;

        // Soft glow behind everything
        juce::ColourGradient glow(Palette::accent.withAlpha(0.18f), cx, cy,
                                 Palette::accent.withAlpha(0.0f),  cx, cy - R - 14.0f, true);
        g.setGradientFill(glow);
        g.fillEllipse(cx - R - 14.0f, cy - R - 14.0f, (R + 14.0f) * 2.0f, (R + 14.0f) * 2.0f);

        // Outer ring
        g.setColour(Palette::accentDim);
        g.drawEllipse(cx - R, cy - R, R * 2.0f, R * 2.0f, 2.0f);

        // Tick ring
        for (int i = 0; i < 90; ++i)
        {
            const float a = static_cast<float>(i) / 90.0f * twoPi;
            const bool major = (i % 5 == 0);
            const float r1 = R - 8.0f;
            const float r2 = R - (major ? 19.0f : 14.0f);
            g.setColour(major ? Palette::accent.withAlpha(0.8f) : Palette::accentDim.withAlpha(0.7f));
            g.drawLine(cx + r1 * std::sin(a), cy - r1 * std::cos(a),
                       cx + r2 * std::sin(a), cy - r2 * std::cos(a),
                       major ? 1.6f : 1.0f);
        }

        // Inner rings
        g.setColour(Palette::accentDeep);
        g.drawEllipse(cx - (R - 30.0f), cy - (R - 30.0f), (R - 30.0f) * 2.0f, (R - 30.0f) * 2.0f, 1.0f);
        g.drawEllipse(cx - (R - 62.0f), cy - (R - 62.0f), (R - 62.0f) * 2.0f, (R - 62.0f) * 2.0f, 1.0f);
        g.drawEllipse(cx - (R - 94.0f), cy - (R - 94.0f), (R - 94.0f) * 2.0f, (R - 94.0f) * 2.0f, 1.0f);

        // Crosshair
        g.setColour(Palette::accent.withAlpha(0.55f));
        g.drawLine(cx - R - 8.0f, cy, cx + R + 8.0f, cy, 1.0f);
        g.drawLine(cx, cy - R - 8.0f, cx, cy + R + 8.0f, 1.0f);

        // Top / bottom markers
        {
            juce::Path tri;
            tri.addTriangle(cx - 7.0f, cy - R - 14.0f, cx + 7.0f, cy - R - 14.0f, cx, cy - R - 3.0f);
            tri.addTriangle(cx - 7.0f, cy + R + 14.0f, cx + 7.0f, cy + R + 14.0f, cx, cy + R + 3.0f);
            g.setColour(Palette::accentBright);
            g.fillPath(tri);
        }

        // Radar sweep line
        {
            const float sweep = static_cast<float>(juce::Time::getMillisecondCounter() % 4000u)
                                / 4000.0f * twoPi;
            g.setColour(Palette::accent.withAlpha(0.22f));
            g.drawLine(cx, cy, cx + (R - 6.0f) * std::sin(sweep), cy - (R - 6.0f) * std::cos(sweep), 1.5f);
        }

        // Confidence arc (grows from the top, both directions)
        if (displayConfidence > 0.01f)
        {
            const float half = juce::jlimit(0.0f, 1.0f, displayConfidence) * juce::MathConstants<float>::pi;
            juce::Path conf;
            conf.addCentredArc(cx, cy, R - 2.0f, R - 2.0f, 0.0f, -half, half, true);
            g.setColour(Palette::accent.withAlpha(0.22f));
            g.strokePath(conf, juce::PathStrokeType(11.0f));
            g.setColour(Palette::accentBright);
            g.strokePath(conf, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        // Inner orb
        {
            const float ro = R - 62.0f;
            juce::ColourGradient orbGrad(Palette::accent.withAlpha(0.30f), cx, cy,
                                        Palette::bgDark.withAlpha(0.9f), cx, cy - ro, true);
            g.setGradientFill(orbGrad);
            g.fillEllipse(cx - ro, cy - ro, ro * 2.0f, ro * 2.0f);
        }

        // Skull and crossbones (the hologram version fades in with the voice)
        if (skullSolidScaled.isValid())
        {
            const int sx = static_cast<int>(cx) - skullSolidScaled.getWidth() / 2;
            const int sy = static_cast<int>(cy) - skullSolidScaled.getHeight() / 2;

            g.setOpacity(0.9f);
            g.drawImageAt(skullSolidScaled, sx, sy);

            if (skullHoloScaled.isValid() && skullGlow > 0.02f)
            {
                g.setOpacity(0.75f * skullGlow);
                g.drawImageAt(skullHoloScaled, sx, sy);
            }
            g.setOpacity(1.0f);
        }

        // Correction needle
        if (displayDetectedHz > 60.0f && displayConfidence > 0.3f)
        {
            float normCorrection = juce::jlimit(-1.0f, 1.0f, displayCorrectionCents / 100.0f);
            float angle = normCorrection * juce::MathConstants<float>::halfPi;
            float indicatorLen = (R - 62.0f) * 0.9f * displayConfidence;
            float ix = cx + indicatorLen * std::sin(angle);
            float iy = cy - indicatorLen * std::cos(angle);

            g.setColour(Palette::accent.withAlpha(0.3f));
            g.drawLine(cx, cy, ix, iy, 8.0f);
            g.setColour(Palette::accentBright);
            g.drawLine(cx, cy, ix, iy, 3.0f);
            g.fillEllipse(ix - 5.0f, iy - 5.0f, 10.0f, 10.0f);
        }

        // Detected note (with glow)
        {
            juce::String noteText = HumHouseVocalTuneProcessor::midiNoteToName(displayDetectedMidi);
            if (noteText.isEmpty()) noteText = "--";

            auto noteRect = juce::Rectangle<int>(static_cast<int>(cx - 50.0f),
                                                static_cast<int>(cy + 48.0f), 100, 36);
            g.setFont(juce::Font(juce::FontOptions(28.0f).withStyle("Bold")));
            g.setColour(Palette::accent.withAlpha(0.14f));
            for (int dx = -2; dx <= 2; ++dx)
                for (int dy = -2; dy <= 2; ++dy)
                    if (dx != 0 || dy != 0)
                        g.drawText(noteText, noteRect.translated(dx, dy), juce::Justification::centred, false);
            g.setColour(Palette::textBright);
            g.drawText(noteText, noteRect, juce::Justification::centred, false);

            juce::String tgt = HumHouseVocalTuneProcessor::midiNoteToName(displayTargetMidi);
            g.setColour(Palette::textDim);
            g.setFont(juce::Font(juce::FontOptions(13.0f)));
            g.drawText("Target: " + (tgt.isEmpty() ? "--" : tgt),
                       static_cast<int>(cx - 50.0f), static_cast<int>(cy + 82.0f), 100, 16,
                       juce::Justification::centred, false);
        }
    }

    // --- Heatmap strip (above the piano) ---
    auto heatArea = juce::Rectangle<int>(20, 404, 574, 34);
    g.setColour(Palette::bgSection);
    g.fillRoundedRectangle(heatArea.toFloat(), 4.0f);
    g.setColour(Palette::accentDeep);
    g.drawRoundedRectangle(heatArea.toFloat(), 4.0f, 1.0f);

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
            return static_cast<float>(heatArea.getBottom()) - 2.0f
                 - norm * static_cast<float>(heatArea.getHeight() - 4);
        };

        float yDet = hzToY(det);
        g.setColour(Palette::heatCold.withAlpha(0.7f));
        g.fillRect(x, yDet, 3.0f, 2.0f);

        if (tgt > 60.0f)
        {
            float yTgt = hzToY(tgt);
            g.setColour(Palette::heatWarm.withAlpha(0.9f));
            g.fillRect(x, yTgt, 3.0f, 2.0f);
        }
    }

    g.setColour(Palette::textDim);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("In/Out HeatMap", heatArea.getX() + 6, heatArea.getY() + 2,
               heatArea.getWidth() - 12, 12, juce::Justification::topRight, false);

    // --- Right-bottom panel: meters, readouts, reference ---
    auto sidePanel = juce::Rectangle<float>(604.0f, 404.0f, 168.0f, 200.0f);
    g.setColour(Palette::bgPanel);
    g.fillRoundedRectangle(sidePanel, 8.0f);
    g.setColour(Palette::accentDim);
    g.drawRoundedRectangle(sidePanel, 8.0f, 1.2f);

    // Input / Output level meters
    auto meterArea = juce::Rectangle<int>(612, 412, 152, 30);
    g.setColour(Palette::textDim);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("Input",  meterArea.getX(), meterArea.getY(), 40, 12, juce::Justification::centredLeft, false);
    g.drawText("Output", meterArea.getX(), meterArea.getY() + 14, 40, 12, juce::Justification::centredLeft, false);

    auto drawMeter = [&](float level, int y)
    {
        int meterX = meterArea.getX() + 46;
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

    drawMeter(displayInputLevel,  meterArea.getY() + 3);
    drawMeter(displayOutputLevel, meterArea.getY() + 17);

    // Detected / Target readouts
    g.setColour(Palette::textDim);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));

    juce::String detNote = HumHouseVocalTuneProcessor::midiNoteToName(displayDetectedMidi);
    juce::String tgtNote = HumHouseVocalTuneProcessor::midiNoteToName(displayTargetMidi);
    juce::String corrStr = juce::String(displayCorrectionCents, 1) + " cents";
    juce::String confStr = juce::String(static_cast<int>(displayConfidence * 100.0f)) + "%";

    const int infoX = 612;
    const int infoW = 154;
    g.drawText("Detected: " + (detNote.isEmpty() ? "--" : detNote)
               + "  " + juce::String(displayDetectedHz, 1) + " Hz",
               infoX, 452, infoW, 16, juce::Justification::centredLeft, false);
    g.drawText("Target:   " + (tgtNote.isEmpty() ? "--" : tgtNote)
               + "  " + juce::String(displayTargetHz, 1) + " Hz",
               infoX, 468, infoW, 16, juce::Justification::centredLeft, false);
    g.drawText("Correction: " + corrStr,
               infoX, 484, infoW, 16, juce::Justification::centredLeft, false);
    g.drawText("Confidence: " + confStr,
               infoX, 500, infoW, 16, juce::Justification::centredLeft, false);

    // --- Piano keyboard background and scale-aware highlighting ---
    auto pianoArea = juce::Rectangle<float>(20.0f, 444.0f, 574.0f, 160.0f);
    g.setColour(Palette::bgPanel);
    g.fillRoundedRectangle(pianoArea, 6.0f);
    g.setColour(Palette::accentDim);
    g.drawRoundedRectangle(pianoArea, 6.0f, 1.2f);

    // Get scale and root note from processor
    int rootNote = static_cast<int>(*proc.getAPVTS().getRawParameterValue("rootNote"));
    int scaleType = static_cast<int>(*proc.getAPVTS().getRawParameterValue("scaleType"));

    // Scale definitions (must match AutoTuneEngine.h)
    bool kMajor[12]     = {true,false,true,false,true,true,false,true,false,true,false,true};
    bool kMinor[12]     = {true,false,true,true,false,true,false,true,true,false,true,false};
    bool kChromatic[12] = {true,true,true,true,true,true,true,true,true,true,true,true};

    bool* scale = (scaleType == 1) ? kMinor
               : (scaleType == 2) ? kChromatic
               : kMajor;

    // Calculate which notes are in the scale
    bool noteInScale[12];
    for (int i = 0; i < 12; ++i)
    {
        int noteInOctave = ((i - rootNote + 120) % 12);
        noteInScale[i] = scale[noteInOctave];
    }

    const int pianoX = 28;
    const int pianoY = 452;
    const int whiteW = 78;
    const int whiteH = 144;
    const int blackW = 48;
    const int blackH = 88;

    int whiteIdx[] = { 0, 2, 4, 5, 7, 9, 11 };
    int blackIdx[] = { 1, 3, 6, 8, 10 };

    // Draw white keys with highlighting
    for (int i = 0; i < 7; ++i)
    {
        int noteIdx = whiteIdx[i];
        int x = pianoX + i * (whiteW + 2);
        bool inScale = noteInScale[noteIdx];

        if (inScale)
        {
            g.setColour(Palette::accentBright.withAlpha(0.28f));
        }
        else
        {
            g.setColour(Palette::bgDark.withAlpha(0.55f));
        }
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(pianoY),
                               static_cast<float>(whiteW), static_cast<float>(whiteH), 3.0f);
    }

    // Draw black keys with highlighting
    int blackPositions[] = { 0, 1, 3, 4, 5 };
    for (int i = 0; i < 5; ++i)
    {
        int noteIdx = blackIdx[i];
        int xOff = pianoX + blackPositions[i] * (whiteW + 2) + whiteW - blackW / 2 + 1;
        bool inScale = noteInScale[noteIdx];

        if (inScale)
        {
            g.setColour(Palette::accentBright.withAlpha(0.45f));
        }
        else
        {
            g.setColour(Palette::bgDark.withAlpha(0.72f));
        }
        g.fillRoundedRectangle(static_cast<float>(xOff), static_cast<float>(pianoY),
                               static_cast<float>(blackW), static_cast<float>(blackH), 2.0f);
    }
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
void HumHouseVocalTuneEditor::resized()
{
    auto place = [](juce::Slider& k, juce::Label& l, int x, int y, int size)
    {
        k.setBounds(x, y, size, size);
        l.setBounds(x - 6, y + size - 2, size + 12, 16);
    };

    // --- Left column of small knobs ---
    const int smKnob = 58;
    const int colX = 15;
    place(humanizeKnob,   humanizeLabel,   colX,  60, smKnob);
    place(mixKnob,        mixLabel,        colX, 132, smKnob);
    place(inputGainKnob,  inputGainLabel,  colX, 204, smKnob);
    place(outputGainKnob, outputGainLabel, colX, 276, smKnob);

    // --- Main knobs (right side) ---
    place(speedKnob,   speedLabel,   500,  62, 128);
    place(sustainKnob, sustainLabel, 650,  70,  98);
    place(amountKnob,  amountLabel,  566, 206, 128);

    // --- Scale mode buttons ---
    const int rowY = 368;
    const int rowH = 28;
    majorBtn.setBounds(20,  rowY, 80, rowH);
    minorBtn.setBounds(106, rowY, 80, rowH);
    chromBtn.setBounds(192, rowY, 90, rowH);

    // --- Toggle buttons ---
    stabilizerBtn.setBounds(300, rowY, 130, rowH);
    formantBtn.setBounds(436,    rowY,  90, rowH);
    lowLatBtn.setBounds(532,     rowY, 120, rowH);

    // --- Root note ---
    rootNoteLabel.setBounds(660, rowY, 28, rowH);
    rootNoteBox.setBounds(690,   rowY, 70, rowH);

    // --- Enable / bypass ---
    enableBtn.setBounds(getWidth() - 110, 8, 95, 28);

    // --- Pitch reference knob (bottom right panel) ---
    place(referenceFreqKnob, referenceFreqLabel, 656, 522, 62);

    // --- Piano note buttons ---
    const int pianoX = 28;
    const int pianoY = 452;
    const int whiteW = 78;
    const int whiteH = 144;
    const int blackW = 48;
    const int blackH = 88;

    int whiteIdx[] = { 0, 2, 4, 5, 7, 9, 11 };
    int blackIdx[] = { 1, 3, 6, 8, 10 };

    for (int i = 0; i < 7; ++i)
    {
        auto& btn = noteButtons[static_cast<size_t>(whiteIdx[i])];
        btn.setBounds(pianoX + i * (whiteW + 2), pianoY, whiteW, whiteH);
    }

    int blackPositions[] = { 0, 1, 3, 4, 5 };
    for (int i = 0; i < 5; ++i)
    {
        auto& btn = noteButtons[static_cast<size_t>(blackIdx[i])];
        int xOff = pianoX + blackPositions[i] * (whiteW + 2) + whiteW - blackW / 2 + 1;
        btn.setBounds(xOff, pianoY, blackW, blackH);
        btn.toFront(false);
    }

    rebuildScaledArt();
}
