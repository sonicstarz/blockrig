#include "host/Transport.h"

namespace blockrig
{
namespace
{
/// Taps further apart than this start a new measurement rather than averaging
/// into the last one.
constexpr double kTapTimeoutSeconds = 2.5;
constexpr int kMaxTaps = 6;
} // namespace

void Transport::prepare(double sampleRate)
{
    mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
}

juce::Optional<juce::AudioPlayHead::PositionInfo> Transport::getPosition() const
{
    PositionInfo info;

    info.setBpm(mBpm.load(std::memory_order_relaxed));
    info.setTimeSignature(TimeSignature{mNumerator.load(std::memory_order_relaxed),
                                        mDenominator.load(std::memory_order_relaxed)});

    const auto samples = mTimeInSamples.load(std::memory_order_relaxed);
    info.setTimeInSamples(samples);
    info.setTimeInSeconds(static_cast<double>(samples) / mSampleRate);
    info.setPpqPosition(mPpqPosition.load(std::memory_order_relaxed));

    // Always "playing": a synced delay should keep its timing whether or not a
    // transport is rolling, because there is nothing else to run against here.
    info.setIsPlaying(true);
    info.setIsRecording(false);

    return info;
}

void Transport::advance(int numSamples) noexcept
{
    if (mFollowingHost.load(std::memory_order_relaxed))
        return; // the host is driving the clock

    const auto bpm = mBpm.load(std::memory_order_relaxed);
    const auto beats = static_cast<double>(numSamples) * bpm / (60.0 * mSampleRate);

    mPpqPosition.store(mPpqPosition.load(std::memory_order_relaxed) + beats, std::memory_order_relaxed);
    mTimeInSamples.store(mTimeInSamples.load(std::memory_order_relaxed) + numSamples, std::memory_order_relaxed);
}

void Transport::followHost(const PositionInfo& hostPosition) noexcept
{
    mFollowingHost.store(true, std::memory_order_relaxed);

    if (const auto bpm = hostPosition.getBpm())
        mBpm.store(juce::jlimit(kMinBpm, kMaxBpm, *bpm), std::memory_order_relaxed);

    if (const auto signature = hostPosition.getTimeSignature())
    {
        mNumerator.store(signature->numerator, std::memory_order_relaxed);
        mDenominator.store(signature->denominator, std::memory_order_relaxed);
    }

    if (const auto ppq = hostPosition.getPpqPosition())
        mPpqPosition.store(*ppq, std::memory_order_relaxed);

    if (const auto samples = hostPosition.getTimeInSamples())
        mTimeInSamples.store(*samples, std::memory_order_relaxed);
}

void Transport::renderMetronome(juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    if (!mMetronomeOn.load(std::memory_order_relaxed))
    {
        mClickEnvelope = 0.0;
        mLastBeat = -1;
        return;
    }

    const auto level = mMetronomeLevel.load(std::memory_order_relaxed);
    const auto bpm = mBpm.load(std::memory_order_relaxed);
    const auto numerator = mNumerator.load(std::memory_order_relaxed);
    const auto beatsPerSample = bpm / (60.0 * mSampleRate);
    auto ppq = mPpqPosition.load(std::memory_order_relaxed);

    const auto channels = juce::jmin(2, buffer.getNumChannels());
    const auto decay = std::exp(-1.0 / (0.012 * mSampleRate)); // ~12 ms click

    for (int i = 0; i < numSamples; ++i)
    {
        // A beat boundary crossed since the last sample starts a new click; the
        // downbeat gets a higher pitch so the bar is audible.
        const auto beat = static_cast<int>(std::floor(ppq));

        if (beat != mLastBeat)
        {
            mLastBeat = beat;
            mClickEnvelope = 1.0;
            mClickPhase = 0.0;
            const auto beatInBar = ((beat % numerator) + numerator) % numerator;
            mClickFrequency = beatInBar == 0 ? 1600.0 : 900.0;
        }

        if (mClickEnvelope > 1.0e-4)
        {
            const auto sample =
                static_cast<float>(std::sin(mClickPhase) * mClickEnvelope * level);

            for (int channel = 0; channel < channels; ++channel)
                buffer.setSample(channel, i, buffer.getSample(channel, i) + sample);

            mClickPhase += 2.0 * juce::MathConstants<double>::pi * mClickFrequency / mSampleRate;
            mClickEnvelope *= decay;
        }

        ppq += beatsPerSample;
    }
}

void Transport::setBpm(double bpm)
{
    mBpm.store(juce::jlimit(kMinBpm, kMaxBpm, bpm), std::memory_order_relaxed);
}

void Transport::setTimeSignature(int numerator, int denominator)
{
    mNumerator.store(juce::jlimit(1, 32, numerator), std::memory_order_relaxed);
    mDenominator.store(juce::jlimit(1, 32, denominator), std::memory_order_relaxed);
}

int Transport::tap()
{
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    if (!mTapTimes.isEmpty() && now - mTapTimes.getLast() > kTapTimeoutSeconds)
        mTapTimes.clearQuick();

    mTapTimes.add(now);

    while (mTapTimes.size() > kMaxTaps)
        mTapTimes.remove(0);

    // Average the intervals rather than using the last one, so an unsteady tap
    // still converges instead of jumping around.
    if (mTapTimes.size() >= 2)
    {
        const auto span = mTapTimes.getLast() - mTapTimes.getFirst();
        const auto intervals = mTapTimes.size() - 1;

        if (span > 0.0)
            setBpm(60.0 * intervals / span);
    }

    return mTapTimes.size();
}

void Transport::resetTaps()
{
    mTapTimes.clearQuick();
}

} // namespace blockrig
