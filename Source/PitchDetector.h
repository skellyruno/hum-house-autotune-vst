#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>

namespace humtune
{

// High-quality YIN pitch detector with CMND (Cumulative Mean Normalized
// Difference).  Operates on a ring buffer so detection runs every hop
// without copying.  Designed for real-time vocal tracking at 44.1–96 kHz.
class PitchDetector
{
public:
    void prepare (double sampleRate, int /*blockSize*/)
    {
        sr = sampleRate;

        // Analysis window = 2 × longest expected period.
        // For A1 ≈ 55 Hz we need ~2 × (sr/55) samples.
        windowSize = static_cast<int>(sr / 55.0) * 2;
        halfWindow = windowSize / 2;

        // Ring buffer: 4× window for safe wrap-around reads
        ringSize = windowSize * 4;
        ring.assign(static_cast<size_t>(ringSize), 0.0f);
        writePos = 0;

        // YIN buffers
        diffBuf.resize(static_cast<size_t>(halfWindow), 0.0f);
        cmndBuf.resize(static_cast<size_t>(halfWindow), 0.0f);

        // Median filter history for stability
        medianHistory.fill(0.0f);
        medianIdx = 0;

        detectedHz = 0.0f;
        confidence = 0.0f;
    }

    // Feed samples into the ring buffer.  Call detectPitch() after feeding
    // at least one block.
    void feedSamples (const float* data, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            ring[static_cast<size_t>(writePos)] = data[i];
            writePos = (writePos + 1) % ringSize;
        }
    }

    // Run YIN detection on the most recent windowSize samples.
    // Returns detected fundamental in Hz (0 if unvoiced).
    float detectPitch()
    {
        if (halfWindow < 4)
            return 0.0f;

        // Step 1 — Difference function
        for (int tau = 0; tau < halfWindow; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < halfWindow; ++j)
            {
                int idx0 = wrap(writePos - halfWindow + j);
                int idx1 = wrap(idx0 + tau);
                float d  = ring[static_cast<size_t>(idx0)]
                         - ring[static_cast<size_t>(idx1)];
                sum += d * d;
            }
            diffBuf[static_cast<size_t>(tau)] = sum;
        }

        // Step 2 — Cumulative Mean Normalized Difference (CMND)
        cmndBuf[0] = 1.0f;
        float runningSum = 0.0f;
        for (int tau = 1; tau < halfWindow; ++tau)
        {
            runningSum += diffBuf[static_cast<size_t>(tau)];
            cmndBuf[static_cast<size_t>(tau)] =
                diffBuf[static_cast<size_t>(tau)]
                * static_cast<float>(tau)
                / std::max(runningSum, 1e-12f);
        }

        // Step 3 — Absolute threshold search
        int tauEst = -1;
        int minTau = std::max(2, static_cast<int>(sr / 1500.0));  // cap at ~1500 Hz
        int maxTau = std::min(halfWindow - 1,
                              static_cast<int>(sr / 55.0));       // floor at ~55 Hz

        for (int tau = minTau; tau <= maxTau; ++tau)
        {
            if (cmndBuf[static_cast<size_t>(tau)] < yinThreshold)
            {
                // Walk to the local minimum
                while (tau + 1 <= maxTau
                    && cmndBuf[static_cast<size_t>(tau + 1)]
                     < cmndBuf[static_cast<size_t>(tau)])
                    ++tau;
                tauEst = tau;
                break;
            }
        }

        // If no dip found, pick the global minimum
        if (tauEst < 0)
        {
            float best = 999.0f;
            for (int tau = minTau; tau <= maxTau; ++tau)
            {
                if (cmndBuf[static_cast<size_t>(tau)] < best)
                {
                    best = cmndBuf[static_cast<size_t>(tau)];
                    tauEst = tau;
                }
            }
            if (best > 0.5f)
            {
                detectedHz = 0.0f;
                confidence = 0.0f;
                return 0.0f;   // Unvoiced
            }
        }

        if (tauEst < 1)
        {
            detectedHz = 0.0f;
            confidence = 0.0f;
            return 0.0f;
        }

        // Step 4 — Parabolic interpolation for sub-sample accuracy
        float betterTau = static_cast<float>(tauEst);
        if (tauEst > 0 && tauEst < halfWindow - 1)
        {
            float s0 = cmndBuf[static_cast<size_t>(tauEst - 1)];
            float s1 = cmndBuf[static_cast<size_t>(tauEst)];
            float s2 = cmndBuf[static_cast<size_t>(tauEst + 1)];
            float denom = 2.0f * (2.0f * s1 - s2 - s0);
            if (std::abs(denom) > 1e-9f)
                betterTau += (s0 - s2) / denom;
        }

        float rawHz = static_cast<float>(sr) / betterTau;
        confidence = 1.0f - cmndBuf[static_cast<size_t>(tauEst)];

        // Step 5 — Median filter (5-point) for stability
        medianHistory[static_cast<size_t>(medianIdx)] = rawHz;
        medianIdx = (medianIdx + 1) % kMedianSize;

        detectedHz = medianFiltered();
        return detectedHz;
    }

    float getDetectedHz()  const { return detectedHz; }
    float getConfidence()  const { return confidence; }

    void setThreshold (float t) { yinThreshold = t; }

private:
    static constexpr int kMedianSize = 5;

    int wrap (int idx) const
    {
        return ((idx % ringSize) + ringSize) % ringSize;
    }

    float medianFiltered() const
    {
        std::array<float, kMedianSize> tmp = medianHistory;
        std::sort(tmp.begin(), tmp.end());
        return tmp[kMedianSize / 2];
    }

    double sr = 44100.0;
    int windowSize = 0;
    int halfWindow = 0;
    int ringSize = 0;
    int writePos = 0;

    std::vector<float> ring;
    std::vector<float> diffBuf;
    std::vector<float> cmndBuf;

    float yinThreshold = 0.15f;
    float detectedHz = 0.0f;
    float confidence = 0.0f;

    std::array<float, kMedianSize> medianHistory {};
    int medianIdx = 0;
};

} // namespace humtune
