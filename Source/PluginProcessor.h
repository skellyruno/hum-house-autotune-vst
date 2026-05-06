#pragma once

#include "AutoTuneEngine.h"
#include <JuceHeader.h>

class HumHouseVocalTuneProcessor : public juce::AudioProcessor
{
public:
    HumHouseVocalTuneProcessor();
    ~HumHouseVocalTuneProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override;
    int  getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Readbacks for editor
    float getDetectedHz()       const { return engine.getDetectedHz(); }
    float getTargetHz()         const { return engine.getTargetHz(); }
    float getCorrectionCents()  const { return engine.getCorrectionCents(); }
    float getConfidence()       const { return engine.getConfidence(); }
    int   getDetectedMidiNote() const { return engine.getDetectedMidiNote(); }
    int   getTargetMidiNote()   const { return engine.getTargetMidiNote(); }

    // Input/output levels for meters
    float getInputLevel()  const { return inputLevel.load(); }
    float getOutputLevel() const { return outputLevel.load(); }

    // Note names for display
    static juce::String midiNoteToName (int midiNote)
    {
        if (midiNote < 0) return "";
        static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        int octave = (midiNote / 12) - 1;
        return juce::String(names[midiNote % 12]) + juce::String(octave);
    }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    humtune::AutoTuneEngine engine;

    std::atomic<float> inputLevel  { 0.0f };
    std::atomic<float> outputLevel { 0.0f };

    int currentPreset = 0;

    struct Preset
    {
        juce::String name;
        float speed;
        float amount;
        float humanize;
        float sustain;
        int   scaleType;
        bool  stabilizer;
        bool  formantPreserve;
        bool  lowLatency;
    };

    static const std::vector<Preset>& getPresets();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HumHouseVocalTuneProcessor)
};
