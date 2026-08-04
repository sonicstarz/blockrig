#include "state/GraphState.h"

#include "state/RigState.h"

namespace blockrig
{
namespace graphstate
{
namespace ids = rigstate::ids;

juce::ValueTree toValueTree(const Graph& graph,
                            const std::function<juce::ValueTree(const BlockInstance&)>& describeBlock)
{
    juce::ValueTree tree(ids::graph);

    for (const auto& node : graph.getNodes())
    {
        // A block ringing out is not part of the rig — it is already gone as far
        // as the document is concerned, and saving it would resurrect a deleted
        // block on the next load.
        if (node.isTailing)
            continue;

        juce::ValueTree element(ids::node);

        if (node.block != nullptr && describeBlock)
        {
            const auto described = describeBlock(*node.block);

            for (int i = 0; i < described.getNumProperties(); ++i)
            {
                const auto property = described.getPropertyName(i);
                element.setProperty(property, described.getProperty(property), nullptr);
            }

            for (const auto child : described)
                element.appendChild(child.createCopy(), nullptr);
        }

        // Set last so a describeBlock that also wrote a uid cannot disagree with
        // the graph, which is the authority on identity.
        element.setProperty(ids::blockUid, node.uid, nullptr);
        element.setProperty(ids::col, node.col, nullptr);
        element.setProperty(ids::nodeRow, node.row, nullptr);

        tree.appendChild(element, nullptr);
    }

    for (const auto& wire : graph.getWires())
    {
        // Wires into a tailing node were cut when it was retired; wires out of
        // one would point at a block the document does not contain.
        const auto* from = graph.findNode(wire.fromUid);
        const auto* to = graph.findNode(wire.toUid);

        if ((from != nullptr && from->isTailing) || (to != nullptr && to->isTailing))
            continue;

        juce::ValueTree element(ids::wire);
        element.setProperty(ids::fromUid, wire.fromUid, nullptr);
        element.setProperty(ids::fromPort, wire.fromPort, nullptr);
        element.setProperty(ids::toUid, wire.toUid, nullptr);
        element.setProperty(ids::toPort, wire.toPort, nullptr);
        tree.appendChild(element, nullptr);
    }

    return tree;
}

LoadResult applyStructure(Graph& graph, const juce::ValueTree& graphTree)
{
    LoadResult result;

    if (! graphTree.isValid() || ! graphTree.hasType(ids::graph))
    {
        result.error = "Not a graph document.";
        return result;
    }

    graph.clear();

    for (const auto element : graphTree)
    {
        if (! element.hasType(ids::node))
            continue;

        const auto uid = element.getProperty(ids::blockUid).toString();

        if (uid.isEmpty())
            continue;

        const int col = static_cast<int>(element.getProperty(ids::col, 0));
        const int row = static_cast<int>(element.getProperty(ids::nodeRow, 0));

        // The endpoints always exist; the document only carries their position.
        if (uid == kInputNodeUid || uid == kOutputNodeUid)
        {
            if (auto* endpoint = graph.findNode(uid))
            {
                endpoint->col = col;
                endpoint->row = row;
            }

            continue;
        }

        GraphNode node;
        node.uid = uid;
        node.col = col;
        node.row = row;
        graph.addNode(node);

        result.pending.push_back({uid, element});
    }

    for (const auto element : graphTree)
    {
        if (! element.hasType(ids::wire))
            continue;

        GraphWire wire;
        wire.fromUid = element.getProperty(ids::fromUid).toString();
        wire.fromPort = static_cast<int>(element.getProperty(ids::fromPort, 0));
        wire.toUid = element.getProperty(ids::toUid).toString();
        wire.toPort = static_cast<int>(element.getProperty(ids::toPort, 0));

        if (! graph.addWire(wire))
            ++result.rejectedWires;
    }

    return result;
}

} // namespace graphstate
} // namespace blockrig
