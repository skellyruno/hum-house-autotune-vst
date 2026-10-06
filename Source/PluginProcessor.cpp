#include "PluginProcessor.h"
#include "PluginEditor.h"

// Note names for combo-box choices
static const juce::StringArray kNoteNames {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
static const juce::StringArray kScaleNames {"Major","Minor","Chromatic"};

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------
const std::vector<HumHouseVocalTuneProcessor::Preset>&
HumHouseVocalTuneProcessor::getPresets()
{
    static const std::vector<Preset> presets =
    {
        // name,                   speed, amt, human, sustain, scale, stab,  formant, lowLat
        { "Hard Snap (T-Pain)",    0.00f, 1.0f, 0.00f, 80.0f,  2,    true,  true,    false },
        { "Sharp Correct",         0.05f, 1.0f, 0.00f, 60.0f,  0,    true,  true,    false },
        { "Natural Correct",       0.25f, 0.8f, 0.15f, 50.0f,  0,    true,  true,    false },
        { "Gentle Touch",          0.50f, 0.5f, 0.30f, 40.0f,  0,    true,  true,    false },
        { "Subtle Polish",         0.70f, 0.3f, 0.40f, 30.0f,  0,    true,  true,    false },
        { "Robotic",               0.00f, 1.0f, 0.00f, 100.0f, 2,    false, false,   false },
        { "Live Performance",      0.15f, 0.9f, 0.10f, 50.0f,  0,    true,  true,    true  },
        { "R&B Smooth",            0.10f, 0.9f, 0.05f, 70.0f,  1,    true,  true,    false },
        { "Pop Vocal",             0.08f, 1.0f, 0.02f, 60.0f,  0,    true,  true,    false },
        { "Trap Vocal",            0.00f, 1.0f, 0.00f, 90.0f,  1,    true,  true,    false },
        { "Gospel Sustain",        0.20f, 0.85f, 0.10f, 80.0f, 0,    true,  true,    false },
        { "Lo-Fi Drift",           0.60f, 0.4f, 0.50f, 20.0f,  2,    false, true,    false },
    };
    return presets;
}

// ---------------------------------------------------------------------------
// Parameter layout
// ---------------------------------------------------------------------------
juce::AudioProcessorValueTreeState::ParameterLayout
HumHouseVocalTuneProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"speed", 1}, "Retune Speed",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"amount", 1}, "Correction Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"humanize", 1}, "Humanize",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"sustain", 1}, "Sustain (cents)",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"rootNote", 1}, "Root Note", 0, 11, 0));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"scaleType", 1}, "Scale Type", 0, 2, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"referenceFreq", 1}, "Reference Frequency",
        juce::NormalisableRange<float>(400.0f, 480.0f, 0.1f), 440.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"noteStabilizer", 1}, "Note Stabilizer", true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"formantPreserve", 1}, "Formant Preserve", true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"lowLatency", 1}, "Low Latency", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"enabled", 1}, "Enabled", true));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mix", 1}, "Dry/Wet Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"inputGain", 1}, "Input Gain",
        juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"outputGain", 1}, "Output Gain",
        juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f));

    // Per-note on/off for custom scale
    for (int i = 0; i < 12; ++i)
    {
        auto id = "note" + juce::String(i);
        auto name = kNoteNames[i] + " On";
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{id, 1}, name, true));
    }

    return { params.begin(), params.end() };
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
HumHouseVocalTuneProcessor::HumHouseVocalTuneProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

// ---------------------------------------------------------------------------
// Prepare / Process
// ---------------------------------------------------------------------------
void HumHouseVocalTuneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setLowLatency(*apvts.getRawParameterValue("lowLatency") > 0.5f);
    engine.prepare(sampleRate, samplesPerBlock);
    reportedLatency = engine.getLatencySamples();
    setLatencySamples(reportedLatency);

    // Scratch buffer for the dry signal (allocated here, not on the audio thread)
    dryScratch.setSize(juce::jmax(2, getTotalNumInputChannels()), juce::jmax(1, samplesPerBlock));
}

void HumHouseVocalTuneProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Read parameters
    float speed     = *apvts.getRawParameterValue("speed");
    float amt       = *apvts.getRawParameterValue("amount");
    float hum       = *apvts.getRawParameterValue("humanize");
    float sus       = *apvts.getRawParameterValue("sustain");
    int   root      = static_cast<int>(*apvts.getRawParameterValue("rootNote"));
    int   scale     = static_cast<int>(*apvts.getRawParameterValue("scaleType"));
    float refFreq   = *apvts.getRawParameterValue("referenceFreq");
    bool  stab      = *apvts.getRawParameterValue("noteStabilizer") > 0.5f;
    bool  formant   = *apvts.getRawParameterValue("formantPreserve") > 0.5f;
    bool  lowLat    = *apvts.getRawParameterValue("lowLatency") > 0.5f;
    bool  on        = *apvts.getRawParameterValue("enabled") > 0.5f;
    float mix       = *apvts.getRawParameterValue("mix");
    float inGainDb  = *apvts.getRawParameterValue("inputGain");
    float outGainDb = *apvts.getRawParameterValue("outputGain");

    // Custom scale from per-note toggles
    std::array<bool, 12> customScale;
    for (int i = 0; i < 12; ++i)
        customScale[static_cast<size_t>(i)] =
            *apvts.getRawParameterValue("note" + juce::String(i)) > 0.5f;

    // Check if any note is turned off (custom mode)
    bool anyOff = false;
    for (auto b : customScale)
        if (!b) { anyOff = true; break; }

    engine.setRootNote(root);
    engine.setScaleType(scale);
    engine.setRetuneSpeed(speed);
    engine.setAmount(amt);
    engine.setHumanize(hum);
    engine.setSustain(sus);
    engine.setReferenceFreq(refFreq);
    engine.setNoteStabilizer(stab);
    engine.setFormantPreserve(formant);
    engine.setLowLatency(lowLat);
    engine.setEnabled(on);

    // The delay depends on the Low Latency switch: tell the host when it changes
    if (engine.getLatencySamples() != reportedLatency)
    {
        reportedLatency = engine.getLatencySamples();
        pendingLatency.store(reportedLatency);
        triggerAsyncUpdate();
    }

    if (anyOff)
        engine.setCustomScale(customScale);
    else
        engine.clearCustomScale();

    // Input gain
    float inGain = juce::Decibels::decibelsToGain(inGainDb);
    buffer.applyGain(inGain);

    // Measure input level
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = std::max(peak, buffer.getMagnitude(ch, 0, buffer.getNumSamples()));
        inputLevel.store(peak);
    }

    // Keep a dry copy for the mix. The wet path is delayed by the engine's latency,
    // so the dry copy has to be delayed by exactly the same amount to line up.
    // (It is done every block so the delay line never holds stale audio.)
    auto& dryBuffer = dryScratch;
    dryBuffer.makeCopyOf(buffer, true);
    engine.delayDry(dryBuffer);

    // Process autotune
    engine.process(buffer);

    // Dry/wet mix
    if (mix < 0.999f)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* wet = buffer.getWritePointer(ch);
            const float* dry = dryBuffer.getReadPointer(ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                wet[i] = dry[i] * (1.0f - mix) + wet[i] * mix;
        }
    }

    // Output gain
    float outGain = juce::Decibels::decibelsToGain(outGainDb);
    buffer.applyGain(outGain);

    // Measure output level
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = std::max(peak, buffer.getMagnitude(ch, 0, buffer.getNumSamples()));
        outputLevel.store(peak);
    }
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------
int HumHouseVocalTuneProcessor::getNumPrograms()
{
    return static_cast<int>(getPresets().size());
}

void HumHouseVocalTuneProcessor::setCurrentProgram (int index)
{
    const auto& presets = getPresets();
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return;

    currentPreset = index;
    const auto& p = presets[static_cast<size_t>(index)];

    auto set = [&](const juce::String& id, float val)
    {
        if (auto* param = apvts.getParameter(id))
            param->setValueNotifyingHost(param->convertTo0to1(val));
    };

    set("speed",           p.speed);
    set("amount",          p.amount);
    set("humanize",        p.humanize);
    set("sustain",         p.sustain);
    set("scaleType",       static_cast<float>(p.scaleType));
    set("noteStabilizer",  p.stabilizer ? 1.0f : 0.0f);
    set("formantPreserve", p.formantPreserve ? 1.0f : 0.0f);
    set("lowLatency",      p.lowLatency ? 1.0f : 0.0f);
}

const juce::String HumHouseVocalTuneProcessor::getProgramName (int index)
{
    const auto& presets = getPresets();
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return {};
    return presets[static_cast<size_t>(index)].name;
}

// ---------------------------------------------------------------------------
// State save / restore
// ---------------------------------------------------------------------------
void HumHouseVocalTuneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void HumHouseVocalTuneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// ---------------------------------------------------------------------------
// Editor factory
// ---------------------------------------------------------------------------
juce::AudioProcessorEditor* HumHouseVocalTuneProcessor::createEditor()
{
    return new HumHouseVocalTuneEditor(*this);
}

// ---------------------------------------------------------------------------
// Plugin instantiation
// ---------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HumHouseVocalTuneProcessor();
}
