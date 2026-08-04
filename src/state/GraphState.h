#pragma once

#include <functional>
#include <vector>

#include "host/Graph.h"
#include "state/RigMigration.h"

namespace blockrig
{
namespace graphstate
{

/// Serialisation for the graph half of a v2 rig document (docs/19 §3). The
/// element names and attributes are the ones `rigstate::migrate_1_to_2` writes,
/// so a migrated rig and a saved rig are the same shape by construction.
///
/// Structure and blocks are deliberately separated. Node positions and wires are
/// pure data and round-trip synchronously; plug-in instantiation is asynchronous
/// and belongs to the restore path in RigState, which creates blocks one at a
/// time and hands them back here. That split is what lets the whole of this file
/// be tested without loading a single plug-in.

/// What a node needs before it can host audio: everything the loader must
/// resolve, in the order the nodes appeared.
struct PendingBlock
{
    juce::String uid;
    juce::ValueTree source; ///< the <Node> element, carrying format/identifier/State
};

struct LoadResult
{
    /// Nodes and wires are already in place; every node's `block` is still null.
    std::vector<PendingBlock> pending;

    /// Wires the graph refused — a cycle or a dangling endpoint in the document.
    /// A healthy rig produces none; anything here means the file was corrupt or
    /// hand-edited, and the caller should say so rather than load a silent rig.
    int rejectedWires = 0;

    juce::String error;
};

/// Writes the graph's structure. `describeBlock` turns a live block into the
/// `<Node>` payload — identity, bypass, the opaque state chunk — exactly as the
/// lane's serialiser does today; endpoints have no block and are written bare.
juce::ValueTree toValueTree(const Graph& graph,
                            const std::function<juce::ValueTree(const BlockInstance&)>& describeBlock);

/// Rebuilds `graph` from a `<Graph>` element: clears it, recreates every node at
/// its saved position, and reconnects every wire. Blocks are left null and
/// reported in the result for the caller to instantiate.
LoadResult applyStructure(Graph& graph, const juce::ValueTree& graphTree);

} // namespace graphstate
} // namespace blockrig
