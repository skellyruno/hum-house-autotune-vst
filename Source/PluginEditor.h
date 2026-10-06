#pragma once

#include "HumHouseLookAndFeel.h"
#include "PluginProcessor.h"
#include <JuceHeader.h>

// MetaTune-style GUI for HumHouse Vocal Tune.
//
// Layout (top to bottom):
//   - Top bar: logo, preset selector, bypass
//   - Pitch orb visualizer (centre) + Speed/Sustain/Amount knobs (right)
//   - Mode buttons: Note, Major Hold, Minor Hold | Sustain: None/Short/Mid/Long
//   - Input/Output meters + Tone volume
//   - Piano keyboard (12 notes) with per-note on/off
//   - Bottom bar: scale selector, pitch reference

class HumHouseVocalTuneEditor : public juce::AudioProcessorEditor,
                                public juce::AudioProcessorParameter::Listener,
                                private juce::Timer
{
public:
    explicit HumHouseVocalTuneEditor (HumHouseVocalTuneProcessor&);
    ~HumHouseVocalTuneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Implement listener interface
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;

private:
    void timerCallback() override;

    HumHouseVocalTuneProcessor& proc;
    humtune::HumHouseLookAndFeel lnf;

    // Knobs
    juce::Slider speedKnob, sustainKnob, amountKnob;
    juce::Slider humanizeKnob, mixKnob;
    juce::Slider inputGainKnob, outputGainKnob;
    juce::Slider referenceFreqKnob;

    // Labels for knobs
    juce::Label speedLabel, sustainLabel, amountLabel;
    juce::Label humanizeLabel, mixLabel;
    juce::Label inputGainLabel, outputGainLabel, referenceFreqLabel;

    // Buttons
    juce::TextButton majorBtn {"Major"};
    juce::TextButton minorBtn {"Minor"};
    juce::TextButton chromBtn {"Chromatic"};

    juce::ToggleButton stabilizerBtn {"Note Stabilizer"};
    juce::ToggleButton formantBtn    {"Formant"};
    juce::ToggleButton lowLatBtn     {"Low Latency"};
    juce::ToggleButton enableBtn     {"Enabled"};

    // Root note combo
    juce::ComboBox rootNoteBox;
    juce::Label rootNoteLabel;

    // Piano keyboard note buttons (C..B)
    std::array<juce::TextButton, 12> noteButtons;

    // APVTS attachments
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttach> speedAtt, sustainAtt, amountAtt;
    std::unique_ptr<SliderAttach> humanizeAtt, mixAtt;
    std::unique_ptr<SliderAttach> inputGainAtt, outputGainAtt, referenceFreqAtt;
    std::unique_ptr<ButtonAttach> stabilizerAtt, formantAtt, lowLatAtt, enableAtt;
    std::unique_ptr<ComboAttach>  rootNoteAtt;
    std::array<std::unique_ptr<ButtonAttach>, 12> noteAtts;

    // Pitch display state
    float displayDetectedHz      = 0.0f;
    float displayTargetHz        = 0.0f;
    float displayCorrectionCents = 0.0f;
    float displayConfidence      = 0.0f;
    int   displayDetectedMidi    = -1;
    int   displayTargetMidi      = -1;
    float displayInputLevel      = 0.0f;
    float displayOutputLevel     = 0.0f;

    // Pitch heatmap history (scrolling)
    static constexpr int kHeatmapWidth = 200;
    std::array<float, kHeatmapWidth> heatmapDetected {};
    std::array<float, kHeatmapWidth> heatmapTarget {};
    int heatmapWriteIdx = 0;

    void setupKnob (juce::Slider&, juce::Label&, const juce::String& text);

    // Artwork (decoded once from Artwork.h; copies at draw size are rebuilt in resized())
    juce::Image skullSolidImg, skullHoloImg, armImg;
    juce::Image skullSolidScaled, skullHoloScaled, armScaled, armScaledFlipped;
    float skullGlow = 0.0f;
    void rebuildScaledArt();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HumHouseVocalTuneEditor)
};
