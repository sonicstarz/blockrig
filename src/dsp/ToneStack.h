#pragma once

#include <array>

namespace nammodeler
{

/// Direct-form-I biquad with double-precision state. Coefficients are stored
/// pre-normalised by a0, matching AudioDSPTools' convention.
class Biquad
{
public:
    void setCoefficients(double b0, double b1, double b2, double a0, double a1, double a2);
    void reset();

    inline float processSample(float input) noexcept
    {
        const double x = static_cast<double>(input);
        double y = mB0 * x + mB1 * mX1 + mB2 * mX2 - mA1 * mY1 - mA2 * mY2;

        // A NaN or inf would otherwise jam the recursion permanently.
        if (!std::isfinite(y))
        {
            y = 0.0;
            mX1 = mX2 = mY1 = mY2 = 0.0;
        }

        mX2 = mX1;
        mX1 = x;
        mY2 = mY1;
        mY1 = y;

        return static_cast<float>(y);
    }

private:
    double mB0 = 1.0, mB1 = 0.0, mB2 = 0.0, mA1 = 0.0, mA2 = 0.0;
    double mX1 = 0.0, mX2 = 0.0, mY1 = 0.0, mY2 = 0.0;
};

/// The official NAM plugin's "BasicNamToneStack", reimplemented for real-time
/// safety (no allocation, no channel indirection). Constants and RBJ cookbook
/// formulas are copied verbatim from sdatkinson/NeuralAmpModelerPlugin so that
/// captures sound identical A/B'd against the official plugin:
///
///   Bass   low shelf,  150 Hz, Q 0.707, gain = 4 * (knob - 5)  (+/- 20 dB)
///   Mid    peaking,    425 Hz, Q 1.5 cutting / 0.7 boosting, gain = 3 * (knob - 5)
///   Treble high shelf, 1800 Hz, Q 0.707, gain = 2 * (knob - 5)  (+/- 10 dB)
///
/// Applied in that order. Knobs run 0-10 with 5 = flat.
class ToneStack
{
public:
    void prepare(double sampleRate);
    void reset();

    /// Recomputes coefficients only when a knob actually moved.
    void setKnobs(float bass, float mid, float treble);

    void processBlock(float* samples, int numSamples) noexcept;

    static constexpr double kBassFrequency = 150.0;
    static constexpr double kBassQuality = 0.707;
    static constexpr double kBassGainPerUnit = 4.0;

    static constexpr double kMidFrequency = 425.0;
    static constexpr double kMidQualityCut = 1.5;
    static constexpr double kMidQualityBoost = 0.7;
    static constexpr double kMidGainPerUnit = 3.0;

    static constexpr double kTrebleFrequency = 1800.0;
    static constexpr double kTrebleQuality = 0.707;
    static constexpr double kTrebleGainPerUnit = 2.0;

    static constexpr float kNeutralKnob = 5.0f;

private:
    void updateCoefficients();

    double mSampleRate = 48000.0;
    float mBassKnob = kNeutralKnob;
    float mMidKnob = kNeutralKnob;
    float mTrebleKnob = kNeutralKnob;
    bool mCoefficientsStale = true;

    Biquad mBass, mMid, mTreble;
};

} // namespace nammodeler
