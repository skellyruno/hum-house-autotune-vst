#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace humtune
{

class PitchShifter
{
public:
    static constexpr double kMinHz = 80.0;
    static constexpr double kMaxHz = 1500.0;

    void prepare (double sampleRate, int /*blockSize*/)
    {
        sr = sampleRate;

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

    void setMinFrequency (double hz)
    {
        hz        = juce::jlimit(kMinHz, 400.0, hz);
        maxPeriod = std::max(32, static_cast<int>(std::ceil(sr / hz)));
        latency   = static_cast<int>(std::ceil(2.5 * maxPeriod)) + 8;
        reset();
    }

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

    int getLatencySamples() const { return latency; }

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
        for (int i = 0; i < m; ++i)
            inRing[static_cast<size_t>((inCount + i) & inMask)] = data[i];
        inCount += m;

        const double hop = periodF / ratio;

        double halfWin = formantPreserve ? periodF : periodF / ratio;
        halfWin = std::min(halfWin, static_cast<double>(latency) - 1.5 * periodF - 8.0);
        halfWin = std::max(halfWin, 0.5 * periodF);
        const double rate = periodF / halfWin;
        const float  gain = static_cast<float>(hop / halfWin);
        const float  piOverW = juce::MathConstants<float>::pi / static_cast<float>(halfWin);

        for (;;)
        {
            const double ta0 = nextTs - static_cast<double>(latency);

            while (markPos + 0.5 * periodF < ta0)
                markPos += periodF;

            if (static_cast<int64_t>(std::floor(markPos + periodF)) + 3 >= inCount)
                break;

            const int64_t oFirst = static_cast<int64_t>(std::ceil (nextTs - halfWin));
            const int64_t oLast  = static_cast<int64_t>(std::floor(nextTs + halfWin));

            for (int64_t o = std::max(oFirst, outRead); o <= oLast; ++o)
            {
                const float x = static_cast<float>(static_cast<double>(o) - nextTs + halfWin);
                if (x <= 0.0f)
                    continue;
                const float w = 0.5f * (1.0f - std::cos(piOverW * x));

                const double  pos = markPos + (static_cast<double>(o) - nextTs) * rate;
                const double  fl  = std::floor(pos);
                const int64_t a   = static_cast<int64_t>(fl);
                if (a < 1)
                    continue;

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

    int64_t inCount = 0;
    int64_t outRead = 0;
    double  nextTs  = 0.0;
    double  markPos = 0.0;
    double  lastPeriod = 220.0;
    bool    formantPreserve = true;
};

} // namespace humtune
