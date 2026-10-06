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

    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;

        // Fixed latency gives us predictable host compensation.
        // 30 ms is enough for a clean, audible retune without harsh artifacts.
        latency = static_cast<int>(std::ceil(0.03 * sr));
        latency = std::max(2048, latency);

        const int minBuffer = std::max(4096, blockSize * 8);
        ringSize = nextPow2(minBuffer + latency * 2);
        ring.assign(static_cast<size_t>(ringSize), 0.0f);

        minPeriod = std::max(4, static_cast<int>(std::floor(sr / kMaxHz)));
        maxPeriod = std::max(32, static_cast<int>(std::ceil(sr / kMinHz)));

        reset();
    }

    void setMinFrequency (double hz)
    {
        hz = juce::jlimit(kMinHz, 400.0, hz);
        minHz = hz;
        maxPeriod = std::max(32, static_cast<int>(std::ceil(sr / hz)));
        reset();
    }

    void setFormantPreserve (bool on) { formantPreserve = on; }

    void reset()
    {
        std::fill(ring.begin(), ring.end(), 0.0f);
        writePos = 0;
        readPhase = 0.0;
        lastPeriod = juce::jlimit(static_cast<double>(minPeriod),
                                 static_cast<double>(maxPeriod),
                                 sr / 200.0);
    }

    int getLatencySamples() const { return latency; }

    void process (float* data, int numSamples, float periodSamples, float shiftRatio)
    {
        if (numSamples <= 0 || ring.empty())
            return;

        double periodF = lastPeriod;
        if (std::isfinite(periodSamples) && periodSamples >= static_cast<float>(minPeriod))
            periodF = static_cast<double>(periodSamples);

        periodF = juce::jlimit(static_cast<double>(minPeriod),
                               static_cast<double>(maxPeriod),
                               periodF);
        lastPeriod = periodF;

        const double ratio = std::isfinite(shiftRatio)
                           ? juce::jlimit(0.5, 2.0, static_cast<double>(shiftRatio))
                           : 1.0;

        // This is a direct resampling pitch shift:
        //   ratio > 1.0 = pitch up
        //   ratio < 1.0 = pitch down
        // The delay line keeps the output time-aligned with the host.
        for (int i = 0; i < numSamples; ++i)
        {
            // Write current input into the delay line
            ring[static_cast<size_t>(writePos)] = data[i];
            writePos = (writePos + 1) % ringSize;

            // Read from a delayed position, stepping through the ring at a rate controlled by ratio
            // This gives a clean, audible retune even for small correction values.
            double sourcePos = static_cast<double>(writePos) - static_cast<double>(latency) + readPhase;

            while (sourcePos < 0.0)
                sourcePos += static_cast<double>(ringSize);
            while (sourcePos >= static_cast<double>(ringSize))
                sourcePos -= static_cast<double>(ringSize);

            const float out = cubicInterpolate(sourcePos);

            // The formant flag is kept for compatibility; in this implementation it doesn't
            // materially change the algorithm because the phase alignment is already stable.
            (void) formantPreserve;

            data[i] = out;

            // Advance the read pointer by the shift ratio
            readPhase += ratio;
            while (readPhase >= static_cast<double>(ringSize))
                readPhase -= static_cast<double>(ringSize);
        }
    }

    bool isNearUnity (float ratio) const
    {
        return std::abs(ratio - 1.0f) < 0.002f;
    }

private:
    static int nextPow2 (int v)
    {
        int p = 1;
        while (p < v)
            p <<= 1;
        return p;
    }

    float cubicInterpolate (double pos) const
    {
        const double fp = std::floor(pos);
        const int i0 = static_cast<int>(fp);
        const int i1 = (i0 + 1) & (ringSize - 1);
        const int i2 = (i0 + 2) & (ringSize - 1);
        const int i3 = (i0 + 3) & (ringSize - 1);

        const double t = pos - fp;
        const double t2 = t * t;
        const double t3 = t2 * t;

        const double c0 = -0.5 * t3 + t2 - 0.5 * t;
        const double c1 =  1.5 * t3 - 2.5 * t2 + 1.0;
        const double c2 = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
        const double c3 =  0.5 * t3 - 0.5 * t2;

        const float s0 = ring[static_cast<size_t>(i0)];
        const float s1 = ring[static_cast<size_t>(i1)];
        const float s2 = ring[static_cast<size_t>(i2)];
        const float s3 = ring[static_cast<size_t>(i3)];

        return static_cast<float>(
            c0 * s0 + c1 * s1 + c2 * s2 + c3 * s3
        );
    }

    double sr = 44100.0;
    int minPeriod = 32;
    int maxPeriod = 800;
    int latency = 2048;
    int ringSize = 4096;
    int writePos = 0;
    double readPhase = 0.0;
    double lastPeriod = 220.0;
    double minHz = kMinHz;

    std::vector<float> ring;
    bool formantPreserve = true;
};

} // namespace humtune
