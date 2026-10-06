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

        // Buffers are sized once for the lowest pitch we ever support (kMinHz),
        // so switching modes later never has to allocate.
        const int maxPeriodAlloc = std::max(32, static_cast<int>(std::ceil(sr / kMinHz)));
        const int latencyAlloc   = static_cast<int>(std::ceil(2.5 * maxPeriodAlloc)) + 8;

        inSize  = nextPow2(latencyAlloc + 4 * maxPeriodAlloc + 2 * kChunk);
        outSize = nextPow2(2 * latencyAlloc + 10 * maxPeriodAlloc + 4 * kChunk);
        inMask  = static_cast<int64_t>(inSize - 1);
        outMask = static_cast<int64_t>(outSize - 1);

        inRing.assign(static_cast<size_t>(inSize), 0.0f);
        outRing.assign(static_cast<size_t>(outSize), 0.0f);

        minPeriod = std::max(4, static_cast<int>(std::floor(sr / kMaxHz)));
        setMinFrequency(kMinHz);
    }

    // Lowest pitch handled. Higher = less delay. Clears the internal state.
    // Does not allocate.
    void setMinFrequency (double hz)
    {
        hz        = juce::jlimit(kMinHz, 400.0, hz);
        maxPeriod = std::max(32, static_cast<int>(std::ceil(sr / hz)));
        latency   = static_cast<int>(std::ceil(2.5 * maxPeriod)) + 8;
        reset();
    }

    // true  = keep the singer's formants (natural sound)
    // false = formants move with the pitch (robotic / "chipmunk" sound)
    void setFormantPreserve (bool on) { formantPreserve = on; }

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
        //    Formants kept:  window = 2 periods, source read at normal speed.
        //    Formants moved: window shrinks/grows with the pitch and the source
        //                    is read at speed `ratio` (the grain is resampled).
        //    In both cases the grains are spaced period / ratio apart, which is
        //    what sets the output pitch.
        const double hop = periodF / ratio;

        double halfWin = formantPreserve ? periodF : periodF / ratio;
        // Never ask for more future output than the fixed delay can cover
        halfWin = std::min(halfWin, static_cast<double>(latency) - 1.5 * periodF - 8.0);
        halfWin = std::max(halfWin, 0.5 * periodF);
        const double rate = periodF / halfWin;           // source samples per output sample
        const float  gain = static_cast<float>(hop / halfWin);
        const float  piOverW = juce::MathConstants<float>::pi / static_cast<float>(halfWin);

        for (;;)
        {
            const double ta0 = nextTs - static_cast<double>(latency);

            // Pick the analysis mark (spaced one period apart) nearest to ta0
            while (markPos + 0.5 * periodF < ta0)
                markPos += periodF;

            // The grain reads source audio from markPos - periodF to markPos + periodF
            if (static_cast<int64_t>(std::floor(markPos + periodF)) + 3 >= inCount)
                break;                       // source audio not here yet

            const int64_t oFirst = static_cast<int64_t>(std::ceil (nextTs - halfWin));
            const int64_t oLast  = static_cast<int64_t>(std::floor(nextTs + halfWin));

            for (int64_t o = std::max(oFirst, outRead); o <= oLast; ++o)
            {
                // Hann window, centred on nextTs, total length 2 * halfWin
                const float x = static_cast<float>(static_cast<double>(o) - nextTs + halfWin);
                if (x <= 0.0f)
                    continue;
                const float w = 0.5f * (1.0f - std::cos(piOverW * x));

                // Exact (fractional) source position, cubic interpolation
                const double  pos = markPos + (static_cast<double>(o) - nextTs) * rate;
                const double  fl  = std::floor(pos);
                const int64_t a   = static_cast<int64_t>(fl);
                if (a < 1)
                    continue;                // before the start of the stream

                const float t  = static_cast<float>(pos - fl);
                const float t2 = t * t, t3 = t2 * t;
                const float c0 = -0.5f * t3 + t2 - 0.5f * t;
                const float c1 =  1.5f * t3 - 2.5f * t2 + 1.0f;
                const float c2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
                const float c3 =  0.5f * t3 - 0.5f * t2;

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
    bool    formantPreserve = true;
};

} // namespace humtune
