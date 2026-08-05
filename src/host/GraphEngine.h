#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "host/Graph.h"
#include "host/GraphLane.h"

namespace blockrig
{

/// Runs a Graph. The threading model is the one already proven in BlockChain and
/// before that in the NAM engine, and it is not open to reinterpretation:
///
///   - every edit rebuilds an immutable RenderPlan on the message thread,
///   - the plan is published through an atomic pointer,
///   - the audio thread adopts it at the top of a block,
///   - the retired plan goes to a queue the message thread drains.
///
/// So the audio thread never allocates, locks, frees, or parses. Buffers and
/// delay lines are sized inside compile(); process() only reads and writes.
///
/// This class is deliberately narrow: it owns the plan lifecycle and the render
/// loop, nothing else. Block ownership, editor windows, and the stage/row API
/// the rest of the app still speaks all stay in BlockChain until G5 retires
/// them.
class GraphEngine
{
public:
    GraphEngine();
    ~GraphEngine();

    //==============================================================================
    // Message thread.

    void prepare(double sampleRate, int maxBlockSize);
    void release();

    /// Tells the graph its input is a single channel, so the blocks IN feeds
    /// negotiate mono-in / stereo-out rather than being handed two identical
    /// channels. Re-negotiates anything whose situation changed.
    void setSourceIsMono(bool sourceIsMono);
    bool getSourceIsMono() const noexcept { return mSourceIsMono; }

    /// Walks the graph in topological order telling each block whether what
    /// reaches it is still a single channel. The lane's rule generalized: a node
    /// is mono-fed when *every* incoming branch is mono, so a merge is stereo the
    /// moment any branch is.
    ///
    /// `force` re-prepares everything; otherwise only blocks whose answer changed
    /// are touched, so adding one block does not re-initialise every plug-in in
    /// the rig.
    void prepareGraph(bool force);

    /// Re-reads block latencies and republishes if any changed. Plug-ins report
    /// latency late and change it when their settings do.
    bool refreshLatency();

    void setPlayHead(juce::AudioPlayHead* playHead);

    /// Called with true before re-preparing any block that is in the published
    /// plan, and false after. Re-negotiating a bus layout is not audio-safe: the
    /// audio thread may be inside that plug-in's processBlock at that moment.
    /// The owner wires this to AudioProcessor::suspendProcessing.
    std::function<void(bool)> suspendAudio;

    /// The model. Edit it, then call publish().
    Graph& getGraph() noexcept { return mGraph; }
    const Graph& getGraph() const noexcept { return mGraph; }

    /// Recompiles and publishes. Cheap enough to call after every edit.
    void publish();

    /// Removes a block from the signal path but keeps it rendering, silence-fed,
    /// into the destinations it used to feed — so its delay or reverb rings out
    /// instead of cutting dead. Takes ownership of the block for the tail's
    /// duration.
    ///
    /// Unlike the lane's version, the tail passes through whatever is downstream
    /// of it: a delay that fed an amp still tails out through that amp, because
    /// the node never leaves the graph until its window closes.
    ///
    /// `seconds` of 0 removes immediately. Returns false if the uid is not a
    /// removable node.
    ///
    /// Ownership of the block is taken from the graph, so callers do not have to
    /// track lifetimes across the tail window.
    bool retireWithTail(const juce::String& uid, double seconds);

    /// Seconds a removed block keeps rendering, silence-fed, so its delay or
    /// reverb tail rings out instead of cutting dead. 0 disables. Matches the
    /// lane's default so removal behaves as it always has.
    void setTailCarrySeconds(double seconds) { mTailCarrySeconds = seconds; }
    double getTailCarrySeconds() const noexcept { return mTailCarrySeconds; }

    /// How many blocks are currently ringing out. For the CPU meter, which has
    /// to be honest that a tail costs real work.
    int getNumTailingBlocks() const noexcept { return static_cast<int>(mTails.size()); }

    /// Frees plans the audio thread has finished with. Call from a timer, the
    /// same way the lane drains its retirement queue.
    void collectGarbage();

    //==============================================================================
    // Audio thread.

    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept;

    int getLatencySamples() const noexcept
    {
        return mPublishedLatency.load(std::memory_order_acquire);
    }

    const BlockLoad& getTotalLoad() const noexcept { return mTotalLoad; }
    BlockLoad& getTotalLoad() noexcept { return mTotalLoad; }

    int getDropoutCount() const noexcept { return mDropouts.load(std::memory_order_relaxed); }
    void clearDropoutCount() noexcept { mDropouts.store(0, std::memory_order_relaxed); }

    //==============================================================================
    // Lane-shaped surface, so BlockRigProcessor and the existing UI can drive a
    // graph unchanged. Transitional — G5 deletes this block along with
    // host/GraphLane. Every one of these forwards to graphlane or the graph.

    void clear();

    BlockInstance* getBlockByUid(const juce::String& uid) const { return mGraph.getBlockByUid(uid); }
    std::vector<BlockInstance*> getBlocks() const { return mGraph.getBlocks(); }
    int getNumBlocks() const { return mGraph.getNumBlocks(); }

    int getNumStages() const;
    int getNumRows(int stage) const;
    bool isStageSplit(int stage) const;
    std::vector<BlockInstance*> getBlocksInRow(int stage, int row) const;
    BlockInstance* getBlockByIndex(int index) const;
    std::optional<BlockPosition> findBlock(const juce::String& uid) const;

    void insertBlock(std::unique_ptr<BlockInstance> block, BlockPosition position);
    void removeBlock(const juce::String& uid);
    void moveBlock(const juce::String& uid, BlockPosition position);

    /// The lane's name for prepareGraph, kept so call sites do not have to move.
    void prepareLane(bool force) { prepareGraph(force); }

    /// Row gain/pan and stage mode have no graph equivalent and are NOT stored.
    ///
    /// In the lane these were properties of a parallel row. On a graph the same
    /// thing is a Utility block sitting on that branch — which is exactly what
    /// migrate_1_to_2 mints when it converts a dualMono split, so migrated rigs
    /// keep their sound. What is gone is the *lane's editor* for those values.
    ///
    /// These exist so the transitional UI compiles. They deliberately do not
    /// pretend to store anything: a setter that remembered a value the audio
    /// path ignores would be worse than one that plainly does nothing. The
    /// Split A/B panel that drives them is disabled for the same reason — see
    /// MainView::firstSplitStage.
    void setRowGainDb(int, int, float) {}
    void setRowPan(int, int, float) {}
    float getRowGainDb(int, int) const { return 0.0f; }
    float getRowPan(int, int) const { return 0.0f; }

    void setStageMode(int, BlockChain::StageMode) {}
    BlockChain::StageMode getStageMode(int) const { return BlockChain::StageMode::dualMono; }

    void appendEmptyStage() {}

private:
    Graph mGraph;

    double mSampleRate = 48000.0;
    int mMaxBlockSize = 512;
    bool mPrepared = false;
    bool mSourceIsMono = false;
    double mTailCarrySeconds = 4.0;
    juce::AudioPlayHead* mPlayHead = nullptr;

    /// Walks the graph in topological order, calling `visit(block, monoIn)` for
    /// every block whose negotiated width would change. Shared by the decide
    /// pass and the apply pass so the two cannot disagree about who needs
    /// touching. Message thread only, so the indirection costs nothing that
    /// matters.
    void walkWidths(bool force, const std::function<void(BlockInstance&, bool)>& visit) const;

    /// Handed to the audio thread; it takes ownership of whatever it finds here.
    std::atomic<RenderPlan*> mPendingPlan{nullptr};

    /// Only the audio thread reads or writes this.
    RenderPlan* mActivePlan = nullptr;

    /// Plans the audio thread has swapped out, waiting for the message thread to
    /// delete them. Freeing on the audio thread is exactly what this avoids.
    std::atomic<RenderPlan*> mRetiredPlan{nullptr};

    /// Counts plans reclaimed in collectGarbage(). A tail's block is safe to
    /// free once this has advanced past the value recorded when the tail left
    /// the graph — see the comment there.
    int mRetirementsSeen = 0;

    std::atomic<int> mPublishedLatency{0};

    BlockLoad mTotalLoad;
    std::atomic<int> mDropouts{0};

    /// A block ringing out. Held by unique_ptr so `samplesLeft` keeps a stable
    /// address: published plans point straight at it.
    struct Tail
    {
        std::unique_ptr<BlockInstance> block;
        juce::String uid;
        std::atomic<int> samplesLeft{0};

        /// mRetirementsSeen at the moment this tail left the graph.
        int retirementMark = 0;
    };

    std::vector<std::unique_ptr<Tail>> mTails;

    /// Expired tails wait one collectGarbage() cycle before being freed, so a
    /// plan still referencing them has been retired first.
    std::vector<std::unique_ptr<Tail>> mExpiredTails;

    /// Sums one branch into `destination`, applying its alignment delay if it
    /// has one. The delay line is the same circular buffer the lane uses for
    /// row padding.
    void addBranch(juce::AudioBuffer<float>& destination,
                   const juce::AudioBuffer<float>& source,
                   PlanDelay* delay,
                   int numSamples) noexcept;
};

} // namespace blockrig
