#pragma once

#include "PitchDetector.h"
#include "PitchShifter.h"
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>

namespace humtune
{

// Complete auto-tune engine combining:
//   - YIN pitch detection
//   - Scale-aware pitch quantization with configurable snap
//   - Note sustain / hold logic
//   - TD-PSOLA pitch shifting (one shifter per channel)
//   - Smoothed retune speed
//
// IMPORTANT: every sample goes through the shifter, even when no correction is
// needed (then it is simply delayed). That keeps the delay constant, so the
// latency reported to the DAW is always correct. Use delayDry() to give the
// dry signal the same delay before mixing it with the processed signal.
class AutoTuneEngine
{
public:
    // Scale bitmasks: 12 bools for C..B
    static constexpr std::array<bool, 12> kMajor     = {true,false,true,false,true,true,false,true,false,true,false,true};
    static constexpr std::array<bool, 12> kMinor     = {true,false,true,true,false,true,false,true,true,false,true,false};
    static constexpr std::array<bool, 12> kChromatic = {true,true,true,true,true,true,true,true,true,true,true,true};

    static constexpr int kMaxChannels = 2;
    static constexpr float kLowLatencyMinHz = 130.0f;   // about C3

    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        detector.prepare(sampleRate, blockSize);
        for (auto& s : shifters)
            s.prepare(sampleRate, blockSize);

        // The longest delay we can ever need (lowest supported pitch)
        const int maxLatency = static_cast<int>(std::ceil(2.5 * std::ceil(sr / PitchDetector::kMinHz))) + 16;
        for (auto& line : dryLine)
            line.reserve(static_cast<size_t>(maxLatency));

        prepared = true;
        applyLatencyMode();
    }

    int   getLatencySamples() const { return latencySamples; }
    float getMinFrequency()   const { return detector.getMinFrequency(); }

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
    // Low latency: only tracks pitches above ~130 Hz, in exchange for less delay.
    // Safe to call from the audio thread (no allocation). The latency changes,
    // so the host must be told: see getLatencySamples().
    void setLowLatency      (bool on)
    {
        if (on == lowLatency)
            return;
        lowLatency = on;
        if (prepared)
            applyLatencyMode();
    }
    void setCustomScale     (const std::array<bool, 12>& s) { customScale = s; useCustom = true; }
    void clearCustomScale   ()            { useCustom = false; }

    // ---- Readbacks for UI ----
    float getDetectedHz()       const { return lastDetectedHz; }
    float getTargetHz()         const { return lastTargetHz; }
    float getCorrectionCents()  const { return lastCorrectionCents; }
    float getConfidence()       const { return detector.getConfidence(); }
    int   getDetectedMidiNote() const { return lastDetectedMidi; }
    int   getTargetMidiNote()   const { return lastTargetMidi; }

    // Delay a (dry) buffer by exactly the same amount as process() delays audio.
    void delayDry (juce::AudioBuffer<float>& buffer)
    {
        const int n   = buffer.getNumSamples();
        const int nch = std::min(buffer.getNumChannels(), kMaxChannels);
        const int len = latencySamples;
        if (n <= 0 || nch <= 0 || len <= 0)
            return;

        int endPos = dryPos;
        for (int ch = 0; ch < nch; ++ch)
        {
            float* d = buffer.getWritePointer(ch);
            auto& line = dryLine[static_cast<size_t>(ch)];
            int p = dryPos;
            for (int i = 0; i < n; ++i)
            {
                const float x = d[i];
                d[i] = line[static_cast<size_t>(p)];
                line[static_cast<size_t>(p)] = x;
                if (++p >= len)
                    p = 0;
            }
            endPos = p;
        }
        dryPos = endPos;
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numChannels == 0 || numSamples == 0 || sr <= 0.0)
            return;

        float periodSamples = 0.0f;   // 0 = "unknown", shifter keeps its last period
        float shiftRatio    = 1.0f;

        if (!enabled)
        {
            lastDetectedHz = 0.0f;
            lastTargetHz = 0.0f;
            lastCorrectionCents = 0.0f;
            lastDetectedMidi = -1;
            lastTargetMidi = -1;
            smoothedCorrectionCents = 0.0;
            lockedMidiNote = -1;
            holdCounter = 0;
        }
        else
        {
            // --- Pitch detection (mono, channel 0) ---
            detector.feedSamples(buffer.getReadPointer(0), numSamples);
            const float detectedHz = detector.detectPitch();
            lastDetectedHz = detectedHz;

            const bool voiced = detectedHz >= detector.getMinFrequency()
                             && detectedHz <= PitchDetector::kMaxHz
                             && detector.getConfidence() >= 0.3f;

            if (!voiced)
            {
                lastTargetHz = detectedHz;
                lastCorrectionCents = 0.0f;
                lastDetectedMidi = -1;
                lastTargetMidi = -1;

                smoothedCorrectionCents *= 0.95;
                lockedMidiNote = -1;
                holdCounter = 0;
            }
            else
            {
                // --- Note quantization ---
                const float midiNote = 69.0f + 12.0f * std::log2(detectedHz / referenceFreq);
                lastDetectedMidi = static_cast<int>(std::round(midiNote));

                int targetMidi = quantizeToScale(midiNote);

                // Note stabilizer: stay on the locked note while the voice is within sustainCents of it
                if (noteStabilizer && lockedMidiNote >= 0)
                {
                    const float lockedHz = referenceFreq * std::pow(2.0f, (static_cast<float>(lockedMidiNote) - 69.0f) / 12.0f);
                    const float centsDiff = std::abs(1200.0f * std::log2(detectedHz / lockedHz));

                    if (centsDiff < sustainCents)
                    {
                        targetMidi = lockedMidiNote;
                        holdCounter++;
                    }
                    else
                    {
                        holdCounter = 0;
                        lockedMidiNote = targetMidi;
                    }
                }
                else
                {
                    lockedMidiNote = targetMidi;
                }

                lastTargetMidi = targetMidi;
                const float targetHz = referenceFreq * std::pow(2.0f, (static_cast<float>(targetMidi) - 69.0f) / 12.0f);
                lastTargetHz = targetHz;

                // --- Correction ---
                float correctionCents = 1200.0f * std::log2(targetHz / detectedHz);
                lastCorrectionCents = correctionCents;

                correctionCents *= amount;               // correction depth
                correctionCents *= (1.0f - humanize);    // humanize

                // --- Retune speed smoothing (time constant, applied once per block) ---
                double timeConstant;
                if (retuneSpeed < 0.01f)
                    timeConstant = 0.0001;               // nearly instant
                else
                    timeConstant = 0.0005 + static_cast<double>(retuneSpeed) * 0.15;

                const double coeff = std::exp(-static_cast<double>(numSamples) / (sr * timeConstant));
                smoothedCorrectionCents = smoothedCorrectionCents * coeff
                                        + static_cast<double>(correctionCents) * (1.0 - coeff);

                shiftRatio = std::pow(2.0f, static_cast<float>(smoothedCorrectionCents) / 1200.0f);
                if (shifters[0].isNearUnity(shiftRatio))
                    shiftRatio = 1.0f;

                periodSamples = static_cast<float>(sr) / detectedHz;
            }
        }

        // --- Always run the shifter so the delay stays constant ---
        const int nch = std::min(numChannels, kMaxChannels);
        for (int ch = 0; ch < nch; ++ch)
        {
            shifters[static_cast<size_t>(ch)].setFormantPreserve(formantPreserve);
            shifters[static_cast<size_t>(ch)].process(buffer.getWritePointer(ch), numSamples,
                                                      periodSamples, shiftRatio);
        }
    }

private:
    // Re-configures detector, shifters and dry delay for the current latency mode.
    // No memory is allocated (everything was reserved in prepare()).
    void applyLatencyMode()
    {
        const float minHz = lowLatency ? kLowLatencyMinHz : PitchDetector::kMinHz;

        detector.setMinFrequency(minHz);
        for (auto& s : shifters)
            s.setMinFrequency(static_cast<double>(minHz));

        latencySamples = shifters[0].getLatencySamples();

        for (auto& line : dryLine)
            line.assign(static_cast<size_t>(latencySamples), 0.0f);
        dryPos = 0;

        smoothedCorrectionCents = 0.0;
        lockedMidiNote = -1;
        holdCounter = 0;

        lastDetectedHz = lastTargetHz = lastCorrectionCents = 0.0f;
        lastDetectedMidi = lastTargetMidi = -1;
    }

    int quantizeToScale (float midiNote) const
    {
        const auto& scale = useCustom ? customScale
                          : (scaleType == 1) ? kMinor
                          : (scaleType == 2) ? kChromatic
                          : kMajor;

        const int nearest = static_cast<int>(std::round(midiNote));
        const int noteInOctave = ((nearest % 12) - rootNote + 120) % 12;

        if (scale[static_cast<size_t>(noteInOctave)])
            return nearest;

        // Search outward for the nearest in-scale note
        for (int offset = 1; offset <= 6; ++offset)
        {
            const int up   = (noteInOctave + offset) % 12;
            const int down = (noteInOctave - offset + 12) % 12;

            const bool upOk   = scale[static_cast<size_t>(up)];
            const bool downOk = scale[static_cast<size_t>(down)];

            if (upOk && downOk)
            {
                const float fracPart = midiNote - static_cast<float>(nearest);
                return (fracPart >= 0.0f) ? nearest + offset : nearest - offset;
            }
            if (upOk)   return nearest + offset;
            if (downOk) return nearest - offset;
        }

        return nearest;
    }

    double sr = 44100.0;
    PitchDetector detector;
    std::array<PitchShifter, kMaxChannels> shifters;

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
    bool  prepared       = false;
    bool  useCustom      = false;
    std::array<bool, 12> customScale = kChromatic;

    // State
    double smoothedCorrectionCents = 0.0;
    int    lockedMidiNote = -1;
    int    holdCounter    = 0;
    int    latencySamples = 0;

    // Dry delay lines (latency compensation for the dry/wet mix)
    std::array<std::vector<float>, kMaxChannels> dryLine;
    int dryPos = 0;

    // Readbacks
    float lastDetectedHz      = 0.0f;
    float lastTargetHz        = 0.0f;
    float lastCorrectionCents = 0.0f;
    int   lastDetectedMidi    = -1;
    int   lastTargetMidi      = -1;
};

} // namespace humtune
