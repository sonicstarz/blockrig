#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "host/BlockInstance.h"

namespace blockrig
{

/// The audio engine: an ordered lane of blocks between the chain's input and
/// output.
///
/// Deliberately not AudioProcessorGraph. Our topology is a lane (with
/// structured parallel rows planned later), and rolling it ourselves buys three
/// things the graph cannot give us: no per-node CriticalSection on every block,
/// no silence gap while a reconfiguration lands, and exact per-block CPU
/// attribution because we own each process call site. See docs/12-ARCHITECTURE.md.
///
/// Threading model, identical in spirit to the model swap already proven in the
/// NAM engine: every edit builds a new immutable Snapshot on the message
/// thread, publishes it through an atomic pointer, and the audio thread adopts
/// it at the top of a block. The retired snapshot goes to a queue that the
/// message thread drains, so the audio thread never allocates, locks or frees.
class BlockChain
{
public:
    BlockChain();
    ~BlockChain();

    /// Immutable view of the lane that the audio thread renders. Blocks are
    /// owned by the chain's block list, not by the snapshot.
    struct Snapshot
    {
        std::vector<BlockInstance*> blocks;
        int totalLatencySamples = 0;
    };

    //==============================================================================
    // Message-thread API. None of this is real-time safe.

    void prepare(double sampleRate, int maxBlockSize);
    void release();

    /// Takes ownership. The block must already be prepared. Appends at `index`
    /// (or at the end when index is out of range) and republishes the lane.
    void insertBlock(std::unique_ptr<BlockInstance> block, int index);

    /// Removes the block with this uid; it stays alive until the audio thread
    /// has stopped using it, then is destroyed on the message thread.
    void removeBlock(const juce::String& uid);

    void moveBlock(const juce::String& uid, int newIndex);

    /// Destroys the whole lane.
    void clear();

    /// Frees blocks and snapshots the audio thread has finished with. Safe to
    /// call from a timer; also called automatically by every edit.
    void collectGarbage();

    /// Re-reads every block's reported latency and republishes if it changed.
    /// Plug-ins can change latency at any time (lookahead toggles, oversampling)
    /// without telling anyone, so this is polled rather than pushed.
    bool refreshLatency();

    int getNumBlocks() const;
    BlockInstance* getBlockByUid(const juce::String& uid) const;
    BlockInstance* getBlockByIndex(int index) const;
    std::vector<BlockInstance*> getBlocks() const;

    //==============================================================================
    // Audio thread.

    /// Renders the lane in place. `buffer` is the chain's stereo I/O.
    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept;

    /// Total latency of the currently rendering lane, for the host.
    int getLatencySamples() const noexcept { return mPublishedLatency.load(std::memory_order_acquire); }

    /// Whole-callback cost as a fraction of the buffer's time budget.
    const BlockLoad& getTotalLoad() const noexcept { return mTotalLoad; }

    /// Number of blocks whose processing exceeded the buffer budget.
    int getDropoutCount() const noexcept { return mDropouts.load(std::memory_order_relaxed); }
    void clearDropoutCount() noexcept { mDropouts.store(0, std::memory_order_relaxed); }

private:
    void publishSnapshot();
    void retireSnapshot(Snapshot* snapshot) noexcept;

    /// Owns every live block, in lane order. Message thread only.
    std::vector<std::unique_ptr<BlockInstance>> mBlocks;

    /// Blocks removed from the lane but possibly still referenced by a snapshot
    /// the audio thread is using. Freed by collectGarbage once safely retired.
    std::vector<std::unique_ptr<BlockInstance>> mBlocksAwaitingDeletion;

    std::atomic<Snapshot*> mPendingSnapshot{nullptr};
    Snapshot* mActiveSnapshot = nullptr; // audio thread only
    std::atomic<int> mPublishedLatency{0};

    /// Retired snapshots, handed back for the message thread to delete.
    static constexpr int kRetireCapacity = 32;
    juce::AbstractFifo mRetireFifo{kRetireCapacity};
    std::array<Snapshot*, kRetireCapacity> mRetireSlots{};

    double mSampleRate = 48000.0;
    int mMaxBlockSize = 512;
    bool mPrepared = false;

    BlockLoad mTotalLoad;
    std::atomic<int> mDropouts{0};

    juce::AudioBuffer<float> mScratch; // for future parallel rows; sized in prepare

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockChain)
};

} // namespace blockrig
