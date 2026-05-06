#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <vector>

namespace humtune
{

// Period-synchronous TD-PSOLA pitch shifter.
//
// Unlike the previous HumHouse implementation that used fixed-size grains
// (causing phase jumps and metallic artifacts), this engine:
//   1. Uses detected-period-length grains for seamless splicing
//   2. Applies Hann windowing per grain for click-free overlap-add
//   3. Preserves formants by keeping grain shape (spectral envelope) intact
//      while changing the placement rate
//
// The key insight: PSOLA shifts pitch by changing the *rate* at which
// identical pitch-period grains are placed in the output, NOT by resampling.
// This inherently preserves formants because each grain's spectral content
// is untouched.
class PitchShifter
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        maxBlock = blockSize;

        // Analysis buffer: enough for 2 full periods of 55 Hz ≈ 3200 samples @ 48kHz
        int maxPeriod = static_cast<int>(sr / 55.0) + 1;
        analysisSize = maxPeriod * 4;

        analysisRing.assign(static_cast<size_t>(analysisSize), 0.0f);
        analysisWrite = 0;

        // Output accumulator with overlap
        int outSize = maxBlock + maxPeriod * 4;
        outputAccum.assign(static_cast<size_t>(outSize), 0.0f);
        outputRead = 0;

        synthPhase = 0.0;
        lastPeriod = static_cast<int>(sr / 200.0);  // default ~200 Hz
        grainWindow.resize(static_cast<size_t>(maxPeriod * 2), 0.0f);
    }

    // Process a block of audio in-place.
    //   detectedPeriodSamples: period length from pitch detector (sr/f0)
    //   shiftRatio:            target/detected pitch ratio (>1 = shift up)
    void process (float* data, int numSamples,
                  float detectedPeriodSamples, float shiftRatio)
    {
        if (numSamples <= 0)
            return;

        int period = static_cast<int>(detectedPeriodSamples);
        if (period < 4)
            period = lastPeriod;
        else
            lastPeriod = period;

        int grainLen = period * 2;  // 2-period Hann grain

        // Ensure buffers are large enough
        if (static_cast<int>(grainWindow.size()) < grainLen)
            grainWindow.resize(static_cast<size_t>(grainLen), 0.0f);

        // Build Hann window for this grain size
        for (int i = 0; i < grainLen; ++i)
        {
            grainWindow[static_cast<size_t>(i)] =
                0.5f * (1.0f - std::cos(
                    juce::MathConstants<float>::twoPi
                    * static_cast<float>(i) / static_cast<float>(grainLen)));
        }

        // Feed input into analysis ring
        for (int i = 0; i < numSamples; ++i)
        {
            analysisRing[static_cast<size_t>(analysisWrite)] = data[i];
            analysisWrite = (analysisWrite + 1) % analysisSize;
        }

        // Ensure output accumulator is large enough
        int requiredOut = numSamples + grainLen * 2;
        if (static_cast<int>(outputAccum.size()) < requiredOut)
            outputAccum.resize(static_cast<size_t>(requiredOut), 0.0f);

        // Clear the section we'll write to
        for (int i = 0; i < numSamples + grainLen; ++i)
        {
            if (i < static_cast<int>(outputAccum.size()))
                outputAccum[static_cast<size_t>(i)] = 0.0f;
        }

        // Synthesis hop = analysis period / shiftRatio
        // (shifting up → shorter hops → higher repetition rate → higher pitch)
        float analysisHop = static_cast<float>(period);
        float synthHop    = analysisHop / std::max(shiftRatio, 0.25f);

        // Place grains via overlap-add
        float synthPos = 0.0f;
        while (static_cast<int>(synthPos) + grainLen < numSamples + grainLen)
        {
            int outPos = static_cast<int>(synthPos);

            // Read grain from analysis buffer centered on the current analysis position
            float analysisPos = synthPos * shiftRatio;
            int aCenter = ((analysisWrite - numSamples
                          + static_cast<int>(analysisPos))
                          % analysisSize + analysisSize) % analysisSize;

            for (int i = 0; i < grainLen; ++i)
            {
                int aIdx = ((aCenter - period + i) % analysisSize + analysisSize) % analysisSize;
                int oIdx = outPos + i;
                if (oIdx >= 0 && oIdx < static_cast<int>(outputAccum.size()))
                {
                    outputAccum[static_cast<size_t>(oIdx)] +=
                        analysisRing[static_cast<size_t>(aIdx)]
                        * grainWindow[static_cast<size_t>(i)];
                }
            }

            synthPos += synthHop;
        }

        // Read output
        for (int i = 0; i < numSamples; ++i)
            data[i] = outputAccum[static_cast<size_t>(i)];
    }

    // Simpler interface when pitch ratio is very close to 1.0
    bool isNearUnity (float ratio) const
    {
        return std::abs(ratio - 1.0f) < 0.002f;
    }

private:
    double sr = 44100.0;
    int maxBlock = 512;
    int analysisSize = 0;
    int analysisWrite = 0;

    std::vector<float> analysisRing;
    std::vector<float> outputAccum;
    std::vector<float> grainWindow;
    int outputRead = 0;

    double synthPhase = 0.0;
    int lastPeriod = 220;
};

} // namespace humtune
