#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

namespace blockrig
{

/// Pitch tracking for the tuner: audio thread writes input samples into a ring,
/// the message thread runs YIN over the most recent window.
///
/// YIN with parabolic interpolation rather than a plain autocorrelation peak,
/// because guitar notes are rich in harmonics and a naive peak picker lands on
/// the octave above often enough to make a tuner useless. The analysis window is
/// long enough to resolve a low B (~31 Hz on a five-string bass) so drop-tuned
/// instruments work too.
class PitchDetector
{
public:
    static constexpr int kWindowSize = 4096;

    void prepare(double sampleRate)
    {
        mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        mRing.assign(kRingSize, 0.0f);
        mWritePosition.store(0, std::memory_order_relaxed);
    }

    /// Audio thread. Lock-free single-writer ring.
    void push(const float* samples, int numSamples) noexcept
    {
        auto position = mWritePosition.load(std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            mRing[static_cast<size_t>(position)] = samples[i];
            position = (position + 1) % kRingSize;
        }

        mWritePosition.store(position, std::memory_order_release);
    }

    struct Result
    {
        float frequency = 0.0f; ///< 0 when no confident pitch
        float level = 0.0f;     ///< RMS of the analysis window
        float clarity = 0.0f;   ///< 0..1, how periodic the window was
    };

    /// Message thread. Analyses the most recent window.
    Result analyse()
    {
        const auto end = mWritePosition.load(std::memory_order_acquire);

        // Copy the newest window out of the ring.
        for (int i = 0; i < kWindowSize; ++i)
            mWindow[static_cast<size_t>(i)] =
                mRing[static_cast<size_t>((end + kRingSize - kWindowSize + i) % kRingSize)];

        Result result;

        double sumSquares = 0.0;
        for (int i = 0; i < kWindowSize; ++i)
            sumSquares += mWindow[static_cast<size_t>(i)] * mWindow[static_cast<size_t>(i)];
        result.level = static_cast<float>(std::sqrt(sumSquares / kWindowSize));

        // Too quiet to bother: report silence rather than tracking noise.
        if (result.level < 1.0e-4f)
            return result;

        // YIN difference function, then the cumulative-mean normalisation that
        // suppresses the zero-lag trivial minimum.
        constexpr int kMaxLag = kWindowSize / 2;
        const int minLag = juce::jmax(2, static_cast<int>(mSampleRate / 1500.0)); // <= 1500 Hz

        auto& difference = mDifference;

        for (int lag = 1; lag < kMaxLag; ++lag)
        {
            double sum = 0.0;
            for (int i = 0; i < kMaxLag; ++i)
            {
                const auto delta = mWindow[static_cast<size_t>(i)] - mWindow[static_cast<size_t>(i + lag)];
                sum += delta * delta;
            }
            difference[static_cast<size_t>(lag)] = static_cast<float>(sum);
        }

        auto& normalised = mNormalised;
        normalised[0] = 1.0f;
        double runningSum = 0.0;

        for (int lag = 1; lag < kMaxLag; ++lag)
        {
            runningSum += difference[static_cast<size_t>(lag)];
            normalised[static_cast<size_t>(lag)] =
                runningSum > 0.0
                    ? difference[static_cast<size_t>(lag)] * lag / static_cast<float>(runningSum)
                    : 1.0f;
        }

        // First dip under the threshold, refined to its local minimum.
        constexpr float kThreshold = 0.15f;
        int bestLag = 0;

        for (int lag = minLag; lag < kMaxLag - 1; ++lag)
        {
            if (normalised[static_cast<size_t>(lag)] < kThreshold)
            {
                while (lag + 1 < kMaxLag
                       && normalised[static_cast<size_t>(lag + 1)] < normalised[static_cast<size_t>(lag)])
                    ++lag;
                bestLag = lag;
                break;
            }
        }

        if (bestLag == 0)
            return result;

        // Parabolic interpolation around the minimum for sub-sample precision -
        // a cent at 82 Hz is far smaller than one lag step.
        const auto y0 = normalised[static_cast<size_t>(bestLag - 1)];
        const auto y1 = normalised[static_cast<size_t>(bestLag)];
        const auto y2 = normalised[static_cast<size_t>(bestLag + 1)];
        const auto denominator = 2.0f * (y0 - 2.0f * y1 + y2);

        const auto refinedLag = std::abs(denominator) > 1.0e-9f
                                    ? bestLag + (y0 - y2) / denominator
                                    : static_cast<float>(bestLag);

        result.frequency = static_cast<float>(mSampleRate) / refinedLag;
        result.clarity = juce::jlimit(0.0f, 1.0f, 1.0f - y1);
        return result;
    }

private:
    static constexpr int kRingSize = kWindowSize * 2;

    double mSampleRate = 48000.0;
    std::vector<float> mRing = std::vector<float>(kRingSize, 0.0f);
    std::array<float, kWindowSize> mWindow{};
    std::array<float, kWindowSize / 2> mDifference{};
    std::array<float, kWindowSize / 2> mNormalised{};
    std::atomic<int> mWritePosition{0};
};

} // namespace blockrig
