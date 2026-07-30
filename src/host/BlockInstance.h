#pragma once

#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

namespace blockrig
{

/// Rolling CPU cost of one block, expressed as a fraction of the audio
/// callback's time budget (the same thing every DAW's meter shows: 100% means
/// the block alone would consume the whole buffer and cause a dropout).
///
/// Written from the audio thread with relaxed atomics, read by the UI.
class BlockLoad
{
public:
    void reset()
    {
        mAverage.store(0.0f, std::memory_order_relaxed);
        mPeak.store(0.0f, std::memory_order_relaxed);
    }

    /// `fraction` is processingTime / bufferDuration for one block.
    void addMeasurement(float fraction) noexcept
    {
        // Exponential moving average: cheap, and stable enough to read at 15 Hz.
        constexpr float smoothing = 0.05f;
        const float previous = mAverage.load(std::memory_order_relaxed);
        mAverage.store(previous + smoothing * (fraction - previous), std::memory_order_relaxed);

        // Keep the largest spike seen; averages hide the blocks that cause dropouts.
        float peak = mPeak.load(std::memory_order_relaxed);
        while (fraction > peak
               && !mPeak.compare_exchange_weak(peak, fraction, std::memory_order_relaxed,
                                               std::memory_order_relaxed))
        {
        }
    }

    float getAverage() const noexcept { return mAverage.load(std::memory_order_relaxed); }
    float getPeak() const noexcept { return mPeak.load(std::memory_order_relaxed); }

    /// Called by the UI so the displayed peak decays instead of latching forever.
    void clearPeak() noexcept { mPeak.store(0.0f, std::memory_order_relaxed); }

private:
    std::atomic<float> mAverage{0.0f};
    std::atomic<float> mPeak{0.0f};
};

/// One block in the chain: a hosted plug-in (VST3/AU) or a built-in processor,
/// plus the bookkeeping the chain needs around it.
///
/// Owns the timing of its own `processBlock` call, which is why CPU attribution
/// is exact here in a way it could not be inside AudioProcessorGraph.
class BlockInstance
{
public:
    BlockInstance(std::unique_ptr<juce::AudioPluginInstance> plugin, juce::String blockUid);
    ~BlockInstance();

    /// Not real-time safe. Called on the message/loader thread before the block
    /// is published into a chain snapshot.
    /// `sourceIsMono` says the signal arriving here is a single channel - either
    /// the rig's input is mono, or nothing upstream has widened it yet.
    ///
    /// It changes which bus layout we ask for, and that is not cosmetic. Handing a
    /// stereo-in/stereo-out plug-in two identical channels makes it symmetric:
    /// a ping-pong delay cross-feeds L into R and R into L, so with L == R the
    /// two sides evolve identically and it can never ping-pong. Mono in,
    /// stereo out is what a DAW gives a plug-in on a mono track, and it is what
    /// makes such a plug-in generate a stereo image rather than preserve one.
    void prepare(double sampleRate, int maxBlockSize, bool sourceIsMono = false);
    void release();

    /// Audio thread. Processes in place; honours bypass; records its own cost.
    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, double bufferDurationSeconds) noexcept;

    juce::AudioPluginInstance* getPlugin() const noexcept { return mPlugin.get(); }
    const juce::String& getUid() const noexcept { return mUid; }

    juce::String getDisplayName() const;

    /// True when the plug-in could only be configured mono. Such a block is fed
    /// the mono sum and its result copied to both sides, rather than being left
    /// to process the left channel and pass the right through untouched.
    bool isMonoOnly() const noexcept { return mMonoOnly; }

    /// What this block was told about its input, so the chain can tell whether a
    /// re-negotiation is needed without re-preparing everything.
    bool getSourceIsMono() const noexcept { return mSourceIsMono; }

    /// A block that has never been prepared must be, whatever the lane thinks
    /// changed - an unprepared plug-in has unsized buffers and crashes on its
    /// first process call.
    bool hasBeenPrepared() const noexcept { return mHasPrepared; }

    /// True when the plug-in emits two channels, which is what stops the signal
    /// counting as mono for everything downstream.
    bool producesStereo() const noexcept { return mProducesStereo; }

    void setBypassed(bool shouldBypass) noexcept { mBypassed.store(shouldBypass, std::memory_order_relaxed); }
    bool isBypassed() const noexcept { return mBypassed.load(std::memory_order_relaxed); }

    /// Latency the plug-in currently reports. Polled by the chain so mid-flight
    /// changes (lookahead toggles and the like) can trigger a recompensation.
    int getLatencySamples() const noexcept { return mPlugin != nullptr ? mPlugin->getLatencySamples() : 0; }

    BlockLoad& getLoad() noexcept { return mLoad; }
    const BlockLoad& getLoad() const noexcept { return mLoad; }

    /// Peak output level of this block, 0..1+. This is what the tile's meter
    /// shows — CPU cost belongs in the CPU panel, not in something a player
    /// will read as a signal meter.
    float getOutputLevel() const noexcept { return mOutputLevel.load(std::memory_order_relaxed); }

private:
    std::unique_ptr<juce::AudioPluginInstance> mPlugin;
    juce::String mUid;

    /// Set when the plug-in exposes its own bypass parameter, which we prefer
    /// over processBlockBypassed because the plug-in may crossfade or tail out.
    juce::AudioProcessorParameter* mBypassParameter = nullptr;

    std::atomic<bool> mBypassed{false};
    bool mLastAppliedBypass = false;
    bool mMonoOnly = false;
    bool mSourceIsMono = false;
    bool mHasPrepared = false;
    bool mProducesStereo = false;
    juce::AudioBuffer<float> mMonoScratch;

    BlockLoad mLoad;
    std::atomic<float> mOutputLevel{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockInstance)
};

} // namespace blockrig
