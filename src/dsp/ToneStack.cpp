#include "dsp/ToneStack.h"

#include <cmath>

namespace nammodeler
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

struct BiquadInputs
{
    BiquadInputs(double sampleRate, double frequency, double quality, double gainDB)
        : a(std::pow(10.0, gainDB / 40.0))
        , omega0(2.0 * kPi * frequency / sampleRate)
        , cosw(std::cos(omega0))
        , alpha(std::sin(omega0) / (2.0 * quality))
    {
    }

    double a;
    double omega0;
    double cosw;
    double alpha;
};

// RBJ audio EQ cookbook, matching AudioDSPTools' recursive_linear_filter.
void setLowShelf(Biquad& filter, const BiquadInputs& in)
{
    const double ap = in.a + 1.0;
    const double am = in.a - 1.0;
    const double roota2alpha = 2.0 * std::sqrt(in.a) * in.alpha;

    const double b0 = in.a * (ap - am * in.cosw + roota2alpha);
    const double b1 = 2.0 * in.a * (am - ap * in.cosw);
    const double b2 = in.a * (ap - am * in.cosw - roota2alpha);
    const double a0 = ap + am * in.cosw + roota2alpha;
    const double a1 = -2.0 * (am + ap * in.cosw);
    const double a2 = ap + am * in.cosw - roota2alpha;

    filter.setCoefficients(b0, b1, b2, a0, a1, a2);
}

void setPeaking(Biquad& filter, const BiquadInputs& in)
{
    const double b0 = 1.0 + in.alpha * in.a;
    const double b1 = -2.0 * in.cosw;
    const double b2 = 1.0 - in.alpha * in.a;
    const double a0 = 1.0 + in.alpha / in.a;
    const double a1 = -2.0 * in.cosw;
    const double a2 = 1.0 - in.alpha / in.a;

    filter.setCoefficients(b0, b1, b2, a0, a1, a2);
}

void setHighShelf(Biquad& filter, const BiquadInputs& in)
{
    const double ap = in.a + 1.0;
    const double am = in.a - 1.0;
    const double roota2alpha = 2.0 * std::sqrt(in.a) * in.alpha;

    const double b0 = in.a * (ap + am * in.cosw + roota2alpha);
    const double b1 = -2.0 * in.a * (am + ap * in.cosw);
    const double b2 = in.a * (ap + am * in.cosw - roota2alpha);
    const double a0 = ap - am * in.cosw + roota2alpha;
    const double a1 = 2.0 * (am - ap * in.cosw);
    const double a2 = ap - am * in.cosw - roota2alpha;

    filter.setCoefficients(b0, b1, b2, a0, a1, a2);
}
} // namespace

void Biquad::setCoefficients(double b0, double b1, double b2, double a0, double a1, double a2)
{
    mB0 = b0 / a0;
    mB1 = b1 / a0;
    mB2 = b2 / a0;
    mA1 = a1 / a0;
    mA2 = a2 / a0;
}

void Biquad::reset()
{
    mX1 = mX2 = mY1 = mY2 = 0.0;
}

void ToneStack::prepare(double sampleRate)
{
    mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    mCoefficientsStale = true;
    reset();
    updateCoefficients();
}

void ToneStack::reset()
{
    mBass.reset();
    mMid.reset();
    mTreble.reset();
}

void ToneStack::setKnobs(float bass, float mid, float treble)
{
    if (bass == mBassKnob && mid == mMidKnob && treble == mTrebleKnob && !mCoefficientsStale)
        return;

    mBassKnob = bass;
    mMidKnob = mid;
    mTrebleKnob = treble;
    mCoefficientsStale = true;
}

void ToneStack::updateCoefficients()
{
    const double bassGainDB = kBassGainPerUnit * (static_cast<double>(mBassKnob) - kNeutralKnob);
    setLowShelf(mBass, BiquadInputs{mSampleRate, kBassFrequency, kBassQuality, bassGainDB});

    const double midGainDB = kMidGainPerUnit * (static_cast<double>(mMidKnob) - kNeutralKnob);
    // Wider EQ on a mid bump so boosts sound less honky (as the official plugin does).
    const double midQuality = midGainDB < 0.0 ? kMidQualityCut : kMidQualityBoost;
    setPeaking(mMid, BiquadInputs{mSampleRate, kMidFrequency, midQuality, midGainDB});

    const double trebleGainDB = kTrebleGainPerUnit * (static_cast<double>(mTrebleKnob) - kNeutralKnob);
    setHighShelf(mTreble, BiquadInputs{mSampleRate, kTrebleFrequency, kTrebleQuality, trebleGainDB});

    mCoefficientsStale = false;
}

void ToneStack::processBlock(float* samples, int numSamples) noexcept
{
    if (mCoefficientsStale)
        updateCoefficients();

    for (int i = 0; i < numSamples; ++i)
        samples[i] = mTreble.processSample(mMid.processSample(mBass.processSample(samples[i])));
}

} // namespace nammodeler
