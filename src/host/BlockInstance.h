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
    void prepare(double sampleRate, int maxBlockSize);
    void release();

    /// Audio thread. Processes in place; honours bypass; records its own cost.
    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, double bufferDurationSeconds) noexcept;

    juce::AudioPluginInstance* getPlugin() const noexcept { return mPlugin.get(); }
    const juce::String& getUid() const noexcept { return mUid; }

    juce::String getDisplayName() const;

    void setBypassed(bool shouldBypass) noexcept { mBypassed.store(shouldBypass, std::memory_order_relaxed); }
    bool isBypassed() const noexcept { return mBypassed.load(std::memory_order_relaxed); }

    /// Latency the plug-in currently reports. Polled by the chain so mid-flight
    /// changes (lookahead toggles and the like) can trigger a recompensation.
    int getLatencySamples() const noexcept { return mPlugin != nullptr ? mPlugin->getLatencySamples() : 0; }

    BlockLoad& getLoad() noexcept { return mLoad; }
    const BlockLoad& getLoad() const noexcept { return mLoad; }

private:
    std::unique_ptr<juce::AudioPluginInstance> mPlugin;
    juce::String mUid;

    /// Set when the plug-in exposes its own bypass parameter, which we prefer
    /// over processBlockBypassed because the plug-in may crossfade or tail out.
    juce::AudioProcessorParameter* mBypassParameter = nullptr;

    std::atomic<bool> mBypassed{false};
    bool mLastAppliedBypass = false;

    BlockLoad mLoad;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockInstance)
};

} // namespace blockrig
