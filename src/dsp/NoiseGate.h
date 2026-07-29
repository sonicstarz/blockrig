#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace nammodeler
{

/// Mono, allocation-free port of AudioDSPTools' noise gate (the one the official
/// NAM plugin uses), with its default parameters.
///
/// Like the original it is split in two: the trigger measures the *input* level
/// and produces a per-sample gain reduction, which is then applied *after* the
/// amp model, so hiss amplified by a high-gain capture gets shut down too.
class NoiseGate
{
public:
    static constexpr double kTime = 0.01;
    static constexpr double kRatio = 0.1; // Quadratic
    static constexpr double kOpenTime = 0.005;
    static constexpr double kHoldTime = 0.01;
    static constexpr double kCloseTime = 0.05;
    static constexpr double kMinimumLoudnessDB = -120.0;

    void prepare(double sampleRate, int maxBlockSize)
    {
        mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        mGainReductionDB.assign(static_cast<size_t>(std::max(1, maxBlockSize)), 0.0);
        reset();
    }

    void reset()
    {
        mLevel = std::pow(10.0, kMinimumLoudnessDB / 10.0);
        mHolding = true;
        mTimeHeld = 0.0;
        mLastGainReductionDB = 0.0;
        std::fill(mGainReductionDB.begin(), mGainReductionDB.end(), 0.0);
    }

    /// Measures `input` and fills the internal per-sample gain reduction curve.
    void trigger(const float* input, int numSamples, double thresholdDB) noexcept
    {
        const double alpha = std::pow(0.5, 1.0 / (kTime * mSampleRate));
        const double beta = 1.0 - alpha;
        const double dt = 1.0 / mSampleRate;
        const double maxGainReduction = gainReductionFor(kMinimumLoudnessDB, thresholdDB);
        const double dOpen = -maxGainReduction / kOpenTime * dt;  // > 0
        const double dClose = maxGainReduction / kCloseTime * dt; // < 0
        const double minPower = std::pow(10.0, kMinimumLoudnessDB / 10.0);

        const int n = std::min(numSamples, static_cast<int>(mGainReductionDB.size()));

        for (int s = 0; s < n; ++s)
        {
            const double x = static_cast<double>(input[s]);
            mLevel = std::clamp(alpha * mLevel + beta * (x * x), minPower, 1000.0);
            const double levelDB = 10.0 * std::log10(mLevel);

            if (mHolding)
            {
                mGainReductionDB[static_cast<size_t>(s)] = 0.0;
                mLastGainReductionDB = 0.0;

                if (levelDB < thresholdDB)
                {
                    mTimeHeld += dt;
                    if (mTimeHeld >= kHoldTime)
                        mHolding = false;
                }
                else
                {
                    mTimeHeld = 0.0;
                }
            }
            else
            {
                const double target = gainReductionFor(levelDB, thresholdDB);

                if (target > mLastGainReductionDB)
                {
                    mLastGainReductionDB += std::clamp(0.5 * (target - mLastGainReductionDB), 0.0, dOpen);
                    if (mLastGainReductionDB >= 0.0)
                    {
                        mLastGainReductionDB = 0.0;
                        mHolding = true;
                        mTimeHeld = 0.0;
                    }
                }
                else if (target < mLastGainReductionDB)
                {
                    mLastGainReductionDB += std::clamp(0.5 * (target - mLastGainReductionDB), dClose, 0.0);
                    mLastGainReductionDB = std::max(mLastGainReductionDB, maxGainReduction);
                }

                mGainReductionDB[static_cast<size_t>(s)] = mLastGainReductionDB;
            }
        }
    }

    /// Applies the curve computed by the most recent trigger() call.
    void applyGain(float* samples, int numSamples) const noexcept
    {
        const int n = std::min(numSamples, static_cast<int>(mGainReductionDB.size()));
        for (int s = 0; s < n; ++s)
            samples[s] *= static_cast<float>(std::pow(10.0, mGainReductionDB[static_cast<size_t>(s)] / 20.0));
    }

private:
    // Quadratic below the threshold, transparent above it.
    static double gainReductionFor(double levelDB, double thresholdDB)
    {
        if (levelDB >= thresholdDB)
            return 0.0;
        const double over = levelDB - thresholdDB;
        return -kRatio * over * over;
    }

    double mSampleRate = 48000.0;
    double mLevel = 0.0;
    bool mHolding = true;
    double mTimeHeld = 0.0;
    double mLastGainReductionDB = 0.0;
    std::vector<double> mGainReductionDB;
};

} // namespace nammodeler
