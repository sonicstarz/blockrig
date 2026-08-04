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
    std::vector<std::unique_ptr<RenderPlan>> mGarbage;

    std::atomic<int> mPublishedLatency{0};

    /// Sums one branch into `destination`, applying its alignment delay if it
    /// has one. The delay line is the same circular buffer the lane uses for
    /// row padding.
    void addBranch(juce::AudioBuffer<float>& destination,
                   const juce::AudioBuffer<float>& source,
                   PlanDelay* delay,
                   int numSamples) noexcept;
};

} // namespace blockrig
