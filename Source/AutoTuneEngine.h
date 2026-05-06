#pragma once

#include "PitchDetector.h"
#include "PitchShifter.h"
#include <JuceHeader.h>
#include <array>
#include <cmath>

namespace humtune
{

// Complete auto-tune engine combining:
//   - YIN pitch detection
//   - Scale-aware pitch quantization with configurable snap
//   - Note sustain / hold logic
//   - TD-PSOLA pitch shifting
//   - Smoothed retune speed
class AutoTuneEngine
{
public:
    // Scale bitmasks: 12 bools for C..B
    static constexpr std::array<bool, 12> kMajor     = {true,false,true,false,true,true,false,true,false,true,false,true};
    static constexpr std::array<bool, 12> kMinor     = {true,false,true,true,false,true,false,true,true,false,true,false};
    static constexpr std::array<bool, 12> kChromatic = {true,true,true,true,true,true,true,true,true,true,true,true};

    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        detector.prepare(sampleRate, blockSize);
        shifter.prepare(sampleRate, blockSize);

        smoothedCorrectionCents = 0.0;
        lockedMidiNote = -1;
        holdCounter = 0;

        // Latency: half of the YIN analysis window
        latencySamples = static_cast<int>(sr / 55.0);

        // Dry delay line for latency compensation
        dryDelay.resize(static_cast<size_t>(latencySamples * 2 + blockSize), 0.0f);
        dryDelayWrite = 0;
        dryDelayRead  = 0;
    }

    int getLatencySamples() const { return latencySamples; }

    // ---- Parameter setters ----
    void setRootNote        (int note)    { rootNote = note % 12; }
    void setScaleType       (int type)    { scaleType = type; }
    void setRetuneSpeed     (float spd)   { retuneSpeed = spd; }      // 0..1 (0=instant, 1=slow)
    void setAmount          (float amt)   { amount = amt; }           // 0..1 (correction depth)
    void setHumanize        (float h)     { humanize = h; }           // 0..1
    void setSustain         (float s)     { sustainCents = s; }       // cents threshold for hold
    void setNoteStabilizer  (bool on)     { noteStabilizer = on; }
    void setFormantPreserve (bool on)     { formantPreserve = on; }
    void setReferenceFreq   (float hz)    { referenceFreq = hz; }
    void setEnabled         (bool on)     { enabled = on; }
    void setLowLatency      (bool on)     { lowLatency = on; }
    void setCustomScale     (const std::array<bool, 12>& s) { customScale = s; useCustom = true; }
    void clearCustomScale   ()            { useCustom = false; }

    // ---- Readbacks for UI ----
    float getDetectedHz()       const { return lastDetectedHz; }
    float getTargetHz()         const { return lastTargetHz; }
    float getCorrectionCents()  const { return lastCorrectionCents; }
    float getConfidence()       const { return detector.getConfidence(); }
    int   getDetectedMidiNote() const { return lastDetectedMidi; }
    int   getTargetMidiNote()   const { return lastTargetMidi; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numChannels == 0 || numSamples == 0 || sr <= 0.0)
            return;

        if (!enabled)
        {
            lastDetectedHz = 0.0f;
            lastTargetHz = 0.0f;
            lastCorrectionCents = 0.0f;
            return;
        }

        // --- Pitch Detection (mono, channel 0) ---
        const float* monoIn = buffer.getReadPointer(0);
        detector.feedSamples(monoIn, numSamples);
        float detectedHz = detector.detectPitch();
        lastDetectedHz = detectedHz;

        // If outside vocal range or unvoiced, pass through
        if (detectedHz < 60.0f || detectedHz > 1500.0f || detector.getConfidence() < 0.3f)
        {
            lastTargetHz = detectedHz;
            lastCorrectionCents = 0.0f;
            lastDetectedMidi = -1;
            lastTargetMidi = -1;

            // Decay the smoothed correction toward zero
            smoothedCorrectionCents *= 0.95;
            lockedMidiNote = -1;
            holdCounter = 0;
            return;
        }

        // --- Note Quantization ---
        float midiNote = 69.0f + 12.0f * std::log2(detectedHz / referenceFreq);
        lastDetectedMidi = static_cast<int>(std::round(midiNote));

        // Note stabilizer: if the detected pitch is within sustainCents
        // of the locked note, hold the lock
        int targetMidi = quantizeToScale(midiNote);

        if (noteStabilizer && lockedMidiNote >= 0)
        {
            float lockedHz = referenceFreq * std::pow(2.0f, (static_cast<float>(lockedMidiNote) - 69.0f) / 12.0f);
            float centsDiff = std::abs(1200.0f * std::log2(detectedHz / lockedHz));

            if (centsDiff < sustainCents)
            {
                targetMidi = lockedMidiNote;
                holdCounter++;
            }
            else
            {
                // Release after sustained deviation
                holdCounter = 0;
                lockedMidiNote = targetMidi;
            }
        }
        else
        {
            lockedMidiNote = targetMidi;
        }

        lastTargetMidi = targetMidi;
        float targetHz = referenceFreq * std::pow(2.0f, (static_cast<float>(targetMidi) - 69.0f) / 12.0f);
        lastTargetHz = targetHz;

        // --- Correction Calculation ---
        float correctionCents = 1200.0f * std::log2(targetHz / detectedHz);
        lastCorrectionCents = correctionCents;

        // Apply amount (correction depth)
        correctionCents *= amount;

        // Apply humanize (reduce correction)
        correctionCents *= (1.0f - humanize);

        // --- Retune Speed Smoothing ---
        // retuneSpeed 0 = instant snap (coefficient → 0)
        // retuneSpeed 1 = very slow    (coefficient → 0.999)
        double timeConstant;
        if (retuneSpeed < 0.01f)
            timeConstant = 0.0001;  // Nearly instant
        else
            timeConstant = 0.0005 + static_cast<double>(retuneSpeed) * 0.15;

        double coeff = std::exp(-1.0 / (sr * timeConstant));
        smoothedCorrectionCents = smoothedCorrectionCents * coeff
                                + static_cast<double>(correctionCents) * (1.0 - coeff);

        // --- Pitch Shifting ---
        float shiftRatio = std::pow(2.0f, static_cast<float>(smoothedCorrectionCents) / 1200.0f);

        if (shifter.isNearUnity(shiftRatio))
            return;  // No audible correction needed

        float periodSamples = static_cast<float>(sr) / detectedHz;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            shifter.process(data, numSamples, periodSamples, shiftRatio);
        }
    }

private:
    int quantizeToScale (float midiNote) const
    {
        const auto& scale = useCustom ? customScale
                          : (scaleType == 1) ? kMinor
                          : (scaleType == 2) ? kChromatic
                          : kMajor;

        int nearest = static_cast<int>(std::round(midiNote));
        int noteInOctave = ((nearest % 12) - rootNote + 120) % 12;

        if (scale[static_cast<size_t>(noteInOctave)])
            return nearest;

        // Search outward for nearest in-scale note
        for (int offset = 1; offset <= 6; ++offset)
        {
            int up   = (noteInOctave + offset) % 12;
            int down = (noteInOctave - offset + 12) % 12;

            bool upOk   = scale[static_cast<size_t>(up)];
            bool downOk = scale[static_cast<size_t>(down)];

            if (upOk && downOk)
            {
                // Pick whichever is closer in pitch
                float fracPart = midiNote - static_cast<float>(nearest);
                return (fracPart >= 0.0f) ? nearest + offset : nearest - offset;
            }
            if (upOk)   return nearest + offset;
            if (downOk) return nearest - offset;
        }

        return nearest;
    }

    double sr = 44100.0;
    PitchDetector detector;
    PitchShifter  shifter;

    // Parameters
    int   rootNote       = 0;        // C
    int   scaleType      = 0;        // 0=Major, 1=Minor, 2=Chromatic
    float retuneSpeed    = 0.0f;     // 0=instant, 1=slow
    float amount         = 1.0f;     // correction depth 0..1
    float humanize       = 0.0f;     // 0..1
    float sustainCents   = 50.0f;    // cents threshold for note hold
    bool  noteStabilizer = true;
    bool  formantPreserve = true;
    float referenceFreq  = 440.0f;
    bool  enabled        = true;
    bool  lowLatency     = false;
    bool  useCustom      = false;
    std::array<bool, 12> customScale = kChromatic;

    // State
    double smoothedCorrectionCents = 0.0;
    int    lockedMidiNote = -1;
    int    holdCounter    = 0;
    int    latencySamples = 0;

    // Dry delay line for latency compensation
    std::vector<float> dryDelay;
    int dryDelayWrite = 0;
    int dryDelayRead  = 0;

    // Readbacks
    float lastDetectedHz      = 0.0f;
    float lastTargetHz        = 0.0f;
    float lastCorrectionCents = 0.0f;
    int   lastDetectedMidi    = -1;
    int   lastTargetMidi      = -1;
};

} // namespace humtune
