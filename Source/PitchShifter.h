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
        latency = std::max(512, static_cast<int>(std::ceil(sr * 0.01)));
        
        ringSize = nextPow2(latency * 4 + blockSize * 8);
        inBuffer.assign(static_cast<size_t>(ringSize), 0.0f);
        outBuffer.assign(static_cast<size_t>(ringSize), 0.0f);
        
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
        std::fill(inBuffer.begin(), inBuffer.end(), 0.0f);
        std::fill(outBuffer.begin(), outBuffer.end(), 0.0f);
        inPos = 0;
        outPos = 0;
        lastPeriod = sr / 200.0;
    }

    int getLatencySamples() const { return latency; }

    void process (float* data, int numSamples, float periodSamples, float shiftRatio)
    {
        if (numSamples <= 0 || inBuffer.empty())
            return;

        double periodF = lastPeriod;
        if (std::isfinite(periodSamples) && periodSamples >= static_cast<float>(minPeriod))
            periodF = static_cast<double>(periodSamples);
        
        periodF = juce::jlimit(static_cast<double>(minPeriod),
                               static_cast<double>(maxPeriod),
                               periodF);
        lastPeriod = periodF;

        const double ratio = std::isfinite(shiftRatio)
                           ? juce::jlimit(0.9, 1.1, static_cast<double>(shiftRatio))
                           : 1.0;

        // If ratio is unity, just pass through with delay
        if (std::abs(ratio - 1.0) < 0.0001)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                inBuffer[static_cast<size_t>(inPos)] = data[i];
                inPos = (inPos + 1) % ringSize;
                
                int readPos = (inPos - latency + ringSize) % ringSize;
                data[i] = inBuffer[static_cast<size_t>(readPos)];
            }
            return;
        }

        // Simple time-stretch: read at a variable rate determined by ratio
        for (int i = 0; i < numSamples; ++i)
        {
            inBuffer[static_cast<size_t>(inPos)] = data[i];
            inPos = (inPos + 1) % ringSize;

            // Calculate read position: we want to read at a rate that produces the pitch shift
            int readIdx = (inPos - latency + ringSize) % ringSize;
            
            // The magic: if ratio > 1.0, we read faster through the buffer (pitch up)
            // if ratio < 1.0, we read slower (pitch down)
            // This is done by advancing readPos at rate = 1/ratio
            readPos += 1.0 / ratio;
            
            // Wrap around
            while (readPos >= static_cast<double>(ringSize))
                readPos -= static_cast<double>(ringSize);
            while (readPos < 0.0)
                readPos += static_cast<double>(ringSize);

            // Cubic interpolation for smooth resampling
            int idx = static_cast<int>(readPos);
            double frac = readPos - std::floor(readPos);
            
            float s0 = inBuffer[static_cast<size_t>((idx - 1 + ringSize) % ringSize)];
            float s1 = inBuffer[static_cast<size_t>(idx)];
            float s2 = inBuffer[static_cast<size_t>((idx + 1) % ringSize)];
            float s3 = inBuffer[static_cast<size_t>((idx + 2) % ringSize)];
            
            data[i] = cubicHermite(s0, s1, s2, s3, static_cast<float>(frac));
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
        while (p < v) p <<= 1;
        return p;
    }

    static float cubicHermite(float y0, float y1, float y2, float y3, float t)
    {
        float t2 = t * t;
        float t3 = t2 * t;

        float c0 = y1;
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 1.5f * (y1 - y2) + 0.5f * (y3 - y0);

        return c0 + c1 * t + c2 * t2 + c3 * t3;
    }

    double sr = 44100.0;
    int minPeriod = 32;
    int maxPeriod = 800;
    int latency = 512;
    int ringSize = 4096;
    int inPos = 0;
    double readPos = 0.0;
    double outPos = 0.0;
    double lastPeriod = 220.0;
    double minHz = kMinHz;

    std::vector<float> inBuffer, outBuffer;
    bool formantPreserve = true;
};

} // namespace humtune
