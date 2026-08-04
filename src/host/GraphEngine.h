#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include "host/Graph.h"

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

private:
    Graph mGraph;

    double mSampleRate = 48000.0;
    int mMaxBlockSize = 512;
    bool mPrepared = false;

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
