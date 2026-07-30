#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "host/BlockInstance.h"

namespace blockrig
{

/// Where a block sits in the lane.
struct BlockPosition
{
    int stage = 0; ///< which stage along the chain
    int row = 0;   ///< which parallel row within that stage (0 = A, 1 = B)
    int index = 0; ///< position within the row

    /// Insert a fresh stage here rather than joining the one already at this
    /// index. Without it, adding at the head of a split chain would drop the
    /// block onto side A instead of in front of the split.
    bool newStage = false;
};

/// The audio engine: a chain of stages, each of which is either a single row of
/// blocks or two parallel rows summed together.
///
/// Deliberately not AudioProcessorGraph. Our topology is a lane with structured
/// splits, and rolling it ourselves buys three things the graph cannot give us:
/// no per-node CriticalSection on every block, no silence gap while a
/// reconfiguration lands, and exact per-block CPU attribution because we own
/// each process call site. See docs/12-ARCHITECTURE.md.
///
/// Threading model, the same pattern proven in the NAM engine: every edit builds
/// a new immutable Snapshot on the message thread, publishes it through an
/// atomic pointer, and the audio thread adopts it at the top of a block. The
/// retired snapshot goes to a queue the message thread drains, so the audio
/// thread never allocates, locks or frees.
class BlockChain
{
public:
    BlockChain();
    ~BlockChain();

    static constexpr int kMaxRowsPerStage = 2;

    /// How a split's two rows recombine.
    enum class StageMode
    {
        /// A becomes the left channel, B the right: two amps forming a stereo
        /// pair. Each row is summed to mono and placed on its own side, so the
        /// result is genuinely stereo rather than the same signal twice.
        dualMono = 0,

        /// Both rows see the full stereo signal and are summed, each keeping its
        /// own image. For blending two treatments of the same part.
        parallel = 1
    };

    /// One parallel row inside a rendered stage.
    struct RenderRow
    {
        std::vector<BlockInstance*> blocks;
        float gainLinear = 1.0f;
        float panLeft = 1.0f;  ///< constant-power pan weights
        float panRight = 1.0f;
        int latency = 0;    ///< sum of this row's blocks
        int padSamples = 0; ///< delay so this row lines up with the longest one

        /// Delay line for padSamples, allocated on the message thread and owned
        /// by the snapshot so the audio thread only ever reads and writes it.
        juce::AudioBuffer<float> padBuffer;
        int padWritePosition = 0;
    };

    struct RenderStage
    {
        std::vector<RenderRow> rows;
        int latency = 0; ///< the longest row, which is what the stage costs
        StageMode mode = StageMode::dualMono;
    };

    struct Snapshot
    {
        std::vector<RenderStage> stages;
        int totalLatencySamples = 0;
    };

    //==============================================================================
    // Message-thread API. None of this is real-time safe.

    void prepare(double sampleRate, int maxBlockSize);

    /// Tells the lane the rig's input is a single channel, so the first blocks
    /// negotiate mono in / stereo out rather than being fed two identical
    /// channels. Re-negotiates any block whose situation changed.
    void setSourceIsMono(bool sourceIsMono);
    bool getSourceIsMono() const noexcept { return mSourceIsMono; }
    void release();

    /// Takes ownership. The block must already be prepared.
    void insertBlock(std::unique_ptr<BlockInstance> block, BlockPosition position);

    void removeBlock(const juce::String& uid);
    void moveBlock(const juce::String& uid, BlockPosition position);
    void clear();

    /// Appends an empty stage. Used when restoring a rig, where the stages have
    /// to exist before their blocks can be placed into them.
    void appendEmptyStage();

    /// Turns a single-row stage into two parallel rows, so the user can put a
    /// different amp on each side. Vertically stacked in the lane, because that
    /// is the only arrangement that reads as parallel.
    bool splitStage(int stageIndex);

    /// Collapses a split back to one row, keeping row A and discarding row B's
    /// blocks. Returns false if the stage was not split.
    bool mergeStage(int stageIndex);

    void setStageMode(int stageIndex, StageMode mode);
    StageMode getStageMode(int stageIndex) const;

    void setRowGainDb(int stageIndex, int rowIndex, float gainDb);
    void setRowPan(int stageIndex, int rowIndex, float pan);
    float getRowGainDb(int stageIndex, int rowIndex) const;
    float getRowPan(int stageIndex, int rowIndex) const;

    /// Applied to every block, now and as new ones arrive, so hosted plug-ins can
    /// sync to the rig's tempo.
    void setPlayHead(juce::AudioPlayHead* playHead);

    /// Called with true before re-preparing any block that is in the published
    /// snapshot, and false after.
    ///
    /// Re-negotiating a bus layout and re-preparing a plug-in are not audio-safe:
    /// the audio thread may be inside that plug-in's processBlock at that moment.
    /// The harness has no audio thread, which is exactly why every measurement
    /// there was clean while the live app misbehaved. The owner wires this to
    /// AudioProcessor::suspendProcessing, which takes the callback lock and so
    /// guarantees processBlock is not running and will not start.
    std::function<void(bool)> suspendAudio;

    /// Walks the lane in order, telling each block whether what reaches it is
    /// still a single channel. `force` re-prepares everything; otherwise only
    /// blocks whose answer changed are touched, so adding one block does not
    /// re-initialise every plug-in in the rig.
    void prepareLane(bool force);

    void collectGarbage();
    bool refreshLatency();

    int getNumStages() const { return static_cast<int>(mLane.size()); }
    int getNumRows(int stageIndex) const;
    bool isStageSplit(int stageIndex) const { return getNumRows(stageIndex) > 1; }

    int getNumBlocks() const;
    BlockInstance* getBlockByUid(const juce::String& uid) const;
    std::vector<BlockInstance*> getBlocks() const;
    std::vector<BlockInstance*> getBlocksInRow(int stageIndex, int rowIndex) const;

    /// Nth block in lane order, counting across stages and rows. Convenience for
    /// tests and for anything that just wants "the first block".
    BlockInstance* getBlockByIndex(int index) const;

    /// Where this block currently is, or nullopt if it is not in the lane.
    std::optional<BlockPosition> findBlock(const juce::String& uid) const;

    //==============================================================================
    // Audio thread.

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept;

    int getLatencySamples() const noexcept { return mPublishedLatency.load(std::memory_order_acquire); }

    const BlockLoad& getTotalLoad() const noexcept { return mTotalLoad; }
    BlockLoad& getTotalLoad() noexcept { return mTotalLoad; }

    int getDropoutCount() const noexcept { return mDropouts.load(std::memory_order_relaxed); }
    void clearDropoutCount() noexcept { mDropouts.store(0, std::memory_order_relaxed); }

private:
    /// Message-thread model of the lane, which owns the blocks.
    struct LaneRow
    {
        std::vector<std::unique_ptr<BlockInstance>> blocks;
        float gainDb = 0.0f;
        /// Default pan for a split: A hard left, B hard right, which is what
        /// people reach for with two amps.
        float pan = 0.0f;
    };

    struct LaneStage
    {
        std::vector<LaneRow> rows{1};
        StageMode mode = StageMode::dualMono;
    };

    void publishSnapshot();
    void retireSnapshot(Snapshot* snapshot) noexcept;
    void processStage(RenderStage& stage, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                      double bufferDuration) noexcept;
    LaneRow* rowAt(int stageIndex, int rowIndex);
    const LaneRow* rowAt(int stageIndex, int rowIndex) const;

    std::vector<LaneStage> mLane;
    std::vector<std::unique_ptr<BlockInstance>> mBlocksAwaitingDeletion;

    std::atomic<Snapshot*> mPendingSnapshot{nullptr};
    Snapshot* mActiveSnapshot = nullptr; // audio thread only
    std::atomic<int> mPublishedLatency{0};

    static constexpr int kRetireCapacity = 32;
    juce::AbstractFifo mRetireFifo{kRetireCapacity};
    std::array<Snapshot*, kRetireCapacity> mRetireSlots{};

    juce::AudioPlayHead* mPlayHead = nullptr;
    double mSampleRate = 48000.0;
    int mMaxBlockSize = 512;
    bool mPrepared = false;
    bool mSourceIsMono = false;

    BlockLoad mTotalLoad;
    std::atomic<int> mDropouts{0};

    /// Audio-thread scratch for parallel rows: one buffer per row plus an
    /// accumulator. Sized in prepare().
    std::array<juce::AudioBuffer<float>, kMaxRowsPerStage> mRowBuffers;
    juce::AudioBuffer<float> mStageInput;
    juce::AudioBuffer<float> mAccumulator;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockChain)
};

} // namespace blockrig
