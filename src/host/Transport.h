#pragma once

#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace blockrig
{

/// The rig's tempo, handed to every hosted plug-in as a playhead.
///
/// Tempo-synced delays and modulation ask the host for the beat; standalone
/// there is no host, so this is it. Inside a DAW the outer host's transport is
/// mirrored into here instead, so the DAW stays the authority and plug-ins see
/// one consistent tempo either way.
class Transport final : public juce::AudioPlayHead
{
public:
    Transport() = default;

    static constexpr double kMinBpm = 20.0;
    static constexpr double kMaxBpm = 300.0;

    juce::Optional<PositionInfo> getPosition() const override;

    void prepare(double sampleRate);

    /// Audio thread: moves the beat clock on by one block.
    void advance(int numSamples) noexcept;

    /// Adopts an outer host's transport, so a DAW's tempo wins over ours.
    void followHost(const PositionInfo& hostPosition) noexcept;

    void setBpm(double bpm);
    double getBpm() const noexcept { return mBpm.load(std::memory_order_relaxed); }

    void setTimeSignature(int numerator, int denominator);
    int getTimeSignatureNumerator() const noexcept { return mNumerator.load(std::memory_order_relaxed); }
    int getTimeSignatureDenominator() const noexcept { return mDenominator.load(std::memory_order_relaxed); }

    /// True while an outer host is supplying the tempo, in which case the UI
    /// shows it read-only rather than pretending the user can change it.
    bool isFollowingHost() const noexcept { return mFollowingHost.load(std::memory_order_relaxed); }
    void setFollowingHost(bool shouldFollow) { mFollowingHost.store(shouldFollow, std::memory_order_relaxed); }

    /// Metronome: on/off and level. Lives here rather than in a block because
    /// the click must not be processed by the rig - it is a reference, not
    /// signal, and running it through an amp would be absurd.
    void setMetronomeEnabled(bool shouldBeEnabled)
    {
        mMetronomeOn.store(shouldBeEnabled, std::memory_order_relaxed);
    }
    bool isMetronomeEnabled() const { return mMetronomeOn.load(std::memory_order_relaxed); }

    void setMetronomeLevel(float level) { mMetronomeLevel.store(level, std::memory_order_relaxed); }
    float getMetronomeLevel() const { return mMetronomeLevel.load(std::memory_order_relaxed); }

    /// Audio thread: adds the click to a buffer that is already at final level.
    /// Returns immediately when the metronome is off.
    void renderMetronome(juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

    /// Registers a tap and, once there are enough, sets the tempo from them.
    /// Returns the number of taps counted so far.
    int tap();
    void resetTaps();

private:
    std::atomic<double> mBpm{120.0};
    std::atomic<int> mNumerator{4};
    std::atomic<int> mDenominator{4};
    std::atomic<bool> mFollowingHost{false};
    std::atomic<bool> mMetronomeOn{false};
    std::atomic<float> mMetronomeLevel{0.35f};

    /// Click voice state, audio thread only.
    double mClickPhase = 0.0;
    double mClickEnvelope = 0.0;
    double mClickFrequency = 1000.0;
    int mLastBeat = -1;

    std::atomic<double> mPpqPosition{0.0};
    std::atomic<juce::int64> mTimeInSamples{0};
    double mSampleRate = 48000.0;

    // Tap tempo state, message thread only.
    juce::Array<double> mTapTimes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Transport)
};

} // namespace blockrig
