#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <optional>

#include <juce_core/juce_core.h>

#include "dsp/AmpSlot.h"

namespace nammodeler
{

/// Owns the background thread that turns .nam data into a live model.
///
/// Parsing JSON, building the network, allocating its buffers and prewarming it
/// are all far too expensive for the audio thread, so they happen here; the
/// finished model is handed to the AmpSlot through its atomic staging pointer.
/// This thread is also the only place models get destroyed.
class ModelLoader : private juce::Thread
{
public:
    static constexpr int kNumSlots = 2;

    ModelLoader();
    ~ModelLoader() override;

    void attachSlot(int slotIndex, AmpSlot* slot);

    /// Host audio configuration; models are (re)built for this rate/block size.
    void setAudioConfiguration(double sampleRate, int maxBlockSize);

    void loadFromFile(int slotIndex, const juce::File& file);
    void loadFromJson(int slotIndex, juce::String json, juce::String name, juce::String path);
    void clearSlot(int slotIndex);

    /// A2 slimmable models only; ignored by everything else. Debounced so a
    /// slider drag does not rebuild on every pixel.
    void setSlimSize(int slotIndex, double value);

    /// Called on the message thread when a load finishes. `error` is empty on
    /// success.
    std::function<void(int slotIndex, ModelInfo info, juce::String error)> onLoadFinished;

private:
    struct Request
    {
        juce::String json;
        juce::String name;
        juce::String path;
        bool clear = false;
    };

    void run() override;
    void processRequest(int slotIndex, Request request);
    void applyPendingSlimSizes();

    std::array<AmpSlot*, kNumSlots> mSlots{};

    juce::CriticalSection mRequestLock;
    std::array<std::optional<Request>, kNumSlots> mPendingRequests;

    std::array<std::atomic<double>, kNumSlots> mSlimTargets{};
    std::array<double, kNumSlots> mAppliedSlim{};

    std::atomic<double> mSampleRate{48000.0};
    std::atomic<int> mMaxBlockSize{512};

    // Queued message-thread callbacks capture this by value and check it, so a
    // load finishing as the plugin closes cannot call into a destroyed loader.
    std::shared_ptr<std::atomic<bool>> mAlive = std::make_shared<std::atomic<bool>>(true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModelLoader)
};

} // namespace nammodeler
