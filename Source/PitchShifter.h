#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace humtune
{

// Streaming TD-PSOLA pitch shifter (one instance per audio channel).
//
// How it works: the input is cut into overlapping, Hann-windowed grains that are
// two pitch periods long, and the grains are laid back down at a different
// spacing (period / ratio). Placing them closer together raises the pitch,
// further apart lowers it, and each grain keeps its own shape so the vocal
// character (formants) is preserved.
//
// Fixes compared with the first version:
//  * Grains that stick out past the end of one audio block are now kept and
//    finished in the next block. Before, every block restarted from silence,
//    which gated the sound at the block rate (heavy buzzing / very quiet).
//  * The input stream is delayed by a fixed number of samples (getLatencySamples)
//    so there is always enough audio ahead of each grain. That delay is the same
//    whether or not any shifting is happening.
//  * Work is done in small chunks, and all memory is allocated in prepare().
class PitchShifter
{
public:
    static constexpr double kMinHz = 80.0;
    static constexpr double kMaxHz = 1500.0;

    void prepare (double sampleRate, int /*blockSize*/)
    {
        sr = sampleRate;
        maxPeriod = std::max(32, static_cast<int>(std::ceil(sr / kMinHz)));
        minPeriod = std::max(4,  static_cast<int>(std::floor(sr / kMaxHz)));
        latency   = static_cast<int>(std::ceil(2.5 * maxPeriod)) + 8;

        inSize  = nextPow2(latency + 4 * maxPeriod + 2 * kChunk);
        outSize = nextPow2(2 * latency + 6 * maxPeriod + 4 * kChunk);
        inMask  = static_cast<int64_t>(inSize - 1);
        outMask = static_cast<int64_t>(outSize - 1);

        inRing.assign(static_cast<size_t>(inSize), 0.0f);
        outRing.assign(static_cast<size_t>(outSize), 0.0f);

        reset();
    }

    void reset()
    {
        std::fill(inRing.begin(), inRing.end(), 0.0f);
        std::fill(outRing.begin(), outRing.end(), 0.0f);
        inCount  = 0;
        outRead  = 0;
        nextTs   = 0.0;
        markPos  = -static_cast<double>(latency);
        lastPeriod = juce::jlimit(static_cast<double>(minPeriod),
                                  static_cast<double>(maxPeriod), sr / 200.0);
    }

    // Fixed delay (in samples) between input and output.
    int getLatencySamples() const { return latency; }

    // Process a block in place.
    //   periodSamples: detected period (sr / f0). Pass 0 if unknown.
    //   shiftRatio:    target pitch / detected pitch (>1 = up). 1 = no change.
    void process (float* data, int numSamples, float periodSamples, float shiftRatio)
    {
        if (numSamples <= 0 || inRing.empty())
            return;

        double periodF = lastPeriod;
        if (std::isfinite(periodSamples) && periodSamples >= static_cast<float>(minPeriod))
            periodF = static_cast<double>(periodSamples);
        periodF = juce::jlimit(static_cast<double>(minPeriod),
                               static_cast<double>(maxPeriod), periodF);
        lastPeriod = periodF;

        const double ratio = std::isfinite(shiftRatio)
                           ? juce::jlimit(0.5, 2.0, static_cast<double>(shiftRatio))
                           : 1.0;

        for (int pos = 0; pos < numSamples; pos += kChunk)
            processChunk(data + pos, std::min(kChunk, numSamples - pos), periodF, ratio);
    }

    // Kept for compatibility with the engine.
    bool isNearUnity (float ratio) const
    {
        return std::abs(ratio - 1.0f) < 0.002f;
    }

private:
    static constexpr int kChunk = 256;

    static int nextPow2 (int v)
    {
        int p = 1;
        while (p < v) p <<= 1;
        return p;
    }

    void processChunk (float* data, int m, double periodF, double ratio)
    {
        // 1) Store the new input
        for (int i = 0; i < m; ++i)
            inRing[static_cast<size_t>((inCount + i) & inMask)] = data[i];
        inCount += m;

        // 2) Lay down every grain whose source audio is now available.
        //    The window is exactly two periods long and is evaluated at the exact
        //    fractional position, so with no pitch change the output is an
        //    exact (delayed) copy of the input.
        const double hop  = periodF / ratio;
        const float  gain = static_cast<float>(hop / periodF);   // = 1 / ratio

        for (;;)
        {
            const double ta0 = nextTs - static_cast<double>(latency);

            // Pick the analysis mark (spaced one period apart) nearest to ta0
            while (markPos + 0.5 * periodF < ta0)
                markPos += periodF;

            // Output sample o is read from input position  o + base  (fractional).
            const double  base    = markPos - nextTs;
            const double  baseFl  = std::floor(base);
            const float   t       = static_cast<float>(base - baseFl);
            const int64_t baseInt = static_cast<int64_t>(baseFl);

            const int64_t oFirst = static_cast<int64_t>(std::ceil (nextTs - periodF));
            const int64_t oLast  = static_cast<int64_t>(std::floor(nextTs + periodF));

            if (baseInt + oLast + 2 >= inCount)
                break;                       // source audio not here yet

            // Catmull-Rom (cubic) interpolation weights for the fractional part
            const float t2 = t * t, t3 = t2 * t;
            const float c0 = -0.5f * t3 + t2 - 0.5f * t;
            const float c1 =  1.5f * t3 - 2.5f * t2 + 1.0f;
            const float c2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
            const float c3 =  0.5f * t3 - 0.5f * t2;

            const float piOverP = juce::MathConstants<float>::pi / static_cast<float>(periodF);

            for (int64_t o = std::max(oFirst, outRead); o <= oLast; ++o)
            {
                const int64_t a = baseInt + o;
                if (a < 1)
                    continue;                // before the start of the stream

                // Hann window, centred on nextTs, total length 2 * periodF
                const float x = static_cast<float>(static_cast<double>(o) - nextTs + periodF);
                if (x <= 0.0f)
                    continue;
                const float w = 0.5f * (1.0f - std::cos(piOverP * x));

                const float s = c0 * inRing[static_cast<size_t>((a - 1) & inMask)]
                              + c1 * inRing[static_cast<size_t>( a      & inMask)]
                              + c2 * inRing[static_cast<size_t>((a + 1) & inMask)]
                              + c3 * inRing[static_cast<size_t>((a + 2) & inMask)];

                outRing[static_cast<size_t>(o & outMask)] += s * w * gain;
            }

            nextTs += hop;
        }

        // 3) Hand finished samples back to the host
        for (int i = 0; i < m; ++i)
        {
            const size_t idx = static_cast<size_t>((outRead + i) & outMask);
            data[i] = outRing[idx];
            outRing[idx] = 0.0f;
        }
        outRead += m;
    }

    double sr = 44100.0;
    int maxPeriod = 800, minPeriod = 32, latency = 2000;
    int inSize = 0, outSize = 0;
    int64_t inMask = 0, outMask = 0;

    std::vector<float> inRing, outRing;

    int64_t inCount = 0;      // total input samples received
    int64_t outRead = 0;      // total output samples sent
    double  nextTs  = 0.0;    // where the next grain's centre goes (output time)
    double  markPos = 0.0;    // current analysis mark (input time)
    double  lastPeriod = 220.0;
};

} // namespace humtune
