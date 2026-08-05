#pragma once

#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "host/BlockInstance.h"

namespace blockrig
{

/// The graph engine's message-thread model: nodes on a snapping grid, wires
/// between them, compiled into an immutable RenderPlan the audio thread adopts.
///
/// Replaces the lane's `stages { rows { blocks } }` shape (docs/19-GRAPH-ENGINE).
/// The threading contract is unchanged and non-negotiable: every edit rebuilds a
/// plan on the message thread, publishes it through an atomic pointer, and the
/// audio thread only ever reads it. Allocation for delay lines and the buffer
/// pool happens here, at compile time, never in process().
///
/// What this file owns is the *shape* of the rig and the compiler that turns it
/// into a render plan. It deliberately does not own the blocks: node identity is
/// a uid, exactly as it is in the lane today, so scenes, MIDI mappings and
/// editor-window memory keep working untouched.

/// Reserved uids for the two endpoint nodes, which are always present and can
/// never be deleted. They sit in the leftmost and rightmost columns.
inline constexpr const char* kInputNodeUid = "__in";
inline constexpr const char* kOutputNodeUid = "__out";

struct GraphNode
{
    juce::String uid;

    /// Grid position. Columns flow left to right and carry signal order; rows
    /// stack parallel paths. Purely presentational — the compiler derives
    /// execution order from the wires, not from the grid — but kept in the model
    /// because it round-trips through the schema and drives the canvas.
    int col = 0;
    int row = 0;

    /// Null for the IN and OUT endpoints, which pass audio without processing.
    BlockInstance* block = nullptr;

    /// Refreshed from the block on the message thread by refreshLatencies().
    /// Held here rather than read through `block` during compilation so the
    /// compiler is pure — which is what lets the alignment maths be tested
    /// without instantiating a single plug-in.
    int latencySamples = 0;

    /// A removed block that is ringing out. Its incoming wires are gone, so it
    /// is fed silence, but it keeps the wires to its former destinations — so a
    /// delay that fed an amp tails out *through* that amp.
    ///
    /// This is what the lane could not do: BlockChain renders each retired block
    /// alone, because a retired snapshot sharing instances with the live one
    /// would process them twice. Here the tailing node is simply still in the
    /// graph, so topology costs nothing extra and the compiler needs no special
    /// case beyond exempting it from IN-reachability.
    bool isTailing = false;

    /// Owned by GraphEngine, stable for the node's lifetime. The audio thread
    /// counts this down; at zero the block stops being processed and the
    /// message thread reclaims it.
    std::atomic<int>* tailSamplesLeft = nullptr;

    bool isEndpoint() const noexcept { return uid == kInputNodeUid || uid == kOutputNodeUid; }
};

/// Ports are always 0 in v1. They exist from the first schema bump so multi-out
/// nodes (Split L/R, a crossover) can arrive later without a second breaking
/// migration — see docs/19 §1.
struct GraphWire
{
    juce::String fromUid;
    int fromPort = 0;
    juce::String toUid;
    int toPort = 0;

    bool operator==(const GraphWire& other) const noexcept
    {
        return fromUid == other.fromUid && fromPort == other.fromPort
            && toUid == other.toUid && toPort == other.toPort;
    }
};

/// One summed input arriving at a node.
struct PlanInput
{
    /// Index into the plan's buffer pool holding the upstream node's output.
    int bufferIndex = 0;

    /// Samples of alignment delay this branch needs so it arrives in phase with
    /// the latest branch feeding the same node. Index into the plan's delay
    /// lines, or -1 when this branch is already the latest and needs no delay.
    int delayIndex = -1;
};

/// One node's worth of work, in execution order.
struct PlanStep
{
    /// Null for endpoints: IN writes the incoming audio, OUT reads the result.
    BlockInstance* block = nullptr;

    juce::String uid;

    /// Branches to sum before running the block. Empty for IN.
    std::vector<PlanInput> inputs;

    /// Buffer this step writes its output into.
    int outputBuffer = 0;

    /// Non-null for a tailing node. The audio thread decrements it each block
    /// and stops calling the block once it reaches zero, leaving the step's
    /// buffer silent.
    std::atomic<int>* tailSamplesLeft = nullptr;
};

/// A delay line sized and cleared at compile time on the message thread; the
/// audio thread only reads and writes within it. Same ownership rule as the
/// lane's per-row padBuffer.
struct PlanDelay
{
    juce::AudioBuffer<float> buffer;
    int lengthSamples = 0;
    int writePosition = 0;
};

/// Immutable once published — except for the sample data inside `buffers` and
/// `delays`, which only the audio thread touches. Everything is sized and
/// cleared at compile time on the message thread.
struct RenderPlan
{
    std::vector<PlanStep> steps;
    std::vector<PlanDelay> delays;

    /// The pool the steps read and write. Allocated by compile(); the audio
    /// thread never resizes it.
    std::vector<juce::AudioBuffer<float>> buffers;

    /// How many pool buffers process() needs. Buffers are reused by liveness, so
    /// this is well below one-per-node on any real rig.
    int numBuffers = 0;

    int totalLatencySamples = 0;

    /// Nodes that do not reach OUT, or are not reached by IN. Legal and dormant:
    /// a parked block is a feature, not an error. They are absent from `steps`,
    /// so they cost nothing; the canvas dims them at 45%.
    std::vector<juce::String> dormantUids;
};

/// Why a wire was refused. The gesture layer turns these into the visible
/// refusal (wire snaps back, target flashes) rather than a silent no-op.
enum class WireRejection
{
    accepted,
    unknownNode,
    selfConnection,
    wouldCycle,
    duplicate
};

class Graph
{
public:
    //==============================================================================
    // Message-thread model. None of this is real-time safe.

    /// Starts with IN and OUT present and unconnected.
    Graph();

    void addNode(GraphNode node);

    /// Adds a node that owns its block. The graph is the owner from here: the
    /// lane's job of keeping BlockInstances alive moves here wholesale, which is
    /// what lets a node be removed without the caller tracking lifetimes.
    ///
    /// The block must already be prepared.
    void addBlockNode(std::unique_ptr<BlockInstance> block, int col, int row);

    /// Attaches a block to a node that already exists — the restore path, where
    /// the structure is rebuilt from the document first and plug-ins arrive
    /// asynchronously afterwards. The node keeps its saved uid, which is what
    /// keeps wires, scenes and MIDI mappings pointing at the right block.
    bool setBlockFor(const juce::String& uid, std::unique_ptr<BlockInstance> block);

    /// Hands a block back out, leaving the node in place but blockless. Used by
    /// spillover, which needs the block to outlive its node.
    std::unique_ptr<BlockInstance> releaseBlock(const juce::String& uid);

    /// Back to a bare IN and OUT, unconnected. Frees every owned block. Used by
    /// the loader before rebuilding from a document.
    void clear();

    //==============================================================================
    // Block access. Position-agnostic, which is most of what the app asks the
    // lane for today — uid lookups and whole-list walks, not stage/row queries.

    BlockInstance* getBlockByUid(const juce::String& uid) const;
    std::vector<BlockInstance*> getBlocks() const;
    int getNumBlocks() const;

    /// Removes the node and every wire touching it. Endpoints refuse removal.
    /// Does not heal the gap — the caller decides whether a removal heals
    /// (drag-out) or leaves a hole; see healAround().
    bool removeNode(const juce::String& uid);

    /// Reconnects each source of `uid` to each destination, then removes it, so
    /// pulling a block out of a chain closes the gap the way removal does today.
    bool healAround(const juce::String& uid);

    WireRejection canAddWire(const GraphWire& wire) const;
    bool addWire(const GraphWire& wire);
    bool removeWire(const GraphWire& wire);

    /// Every wire touching this node, in either direction.
    std::vector<GraphWire> wiresAt(const juce::String& uid) const;

    const std::vector<GraphNode>& getNodes() const noexcept { return mNodes; }
    const std::vector<GraphWire>& getWires() const noexcept { return mWires; }

    GraphNode* findNode(const juce::String& uid);
    const GraphNode* findNode(const juce::String& uid) const;

    /// Pulls each node's current latency from its block. Mirrors the lane's
    /// refreshLatency() poll; plug-ins report latency late and change it when
    /// their settings change. Returns true if any node's latency moved, so the
    /// caller knows whether a republish is needed.
    bool refreshLatencies();

    /// Execution order, or nullopt if the graph contains a cycle. Only nodes
    /// that participate in the signal path appear; dormant nodes are excluded.
    std::optional<std::vector<juce::String>> topologicalOrder() const;

    /// Builds the plan. `maxBlockSize` sizes the delay lines, exactly as the
    /// lane sizes its pad buffers.
    RenderPlan compile(int maxBlockSize) const;

private:
    std::vector<GraphNode> mNodes;
    std::vector<GraphWire> mWires;

    /// Blocks the graph owns. A node's `block` pointer aims into this; removing
    /// a node frees its entry unless the block was released first.
    std::vector<std::unique_ptr<BlockInstance>> mOwnedBlocks;

    void freeBlockFor(const juce::String& uid);

    bool hasNode(const juce::String& uid) const;

    /// Depth-first reachability, used both for cycle rejection and for finding
    /// dormant subgraphs.
    bool reaches(const juce::String& fromUid, const juce::String& targetUid) const;

    std::vector<juce::String> sourcesOf(const juce::String& uid) const;
    std::vector<juce::String> destinationsOf(const juce::String& uid) const;
};

} // namespace blockrig
