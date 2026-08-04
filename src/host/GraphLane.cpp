#include "host/GraphLane.h"

#include <algorithm>
#include <set>

namespace blockrig
{
namespace graphlane
{
namespace
{
/// Nodes the lane should show: real blocks, not endpoints, not ringing out.
bool isVisible(const GraphNode& node)
{
    return node.block != nullptr && ! node.isEndpoint() && ! node.isTailing;
}
} // namespace

std::vector<int> columns(const Graph& graph)
{
    std::set<int> occupied;

    for (const auto& node : graph.getNodes())
        if (isVisible(node))
            occupied.insert(node.col);

    return {occupied.begin(), occupied.end()};
}

int getNumStages(const Graph& graph)
{
    return static_cast<int>(columns(graph).size());
}

int getNumRows(const Graph& graph, int stage)
{
    const auto occupied = columns(graph);

    if (stage < 0 || stage >= static_cast<int>(occupied.size()))
        return 0;

    const int column = occupied[static_cast<size_t>(stage)];

    // Rows are counted by the highest occupied row, not by how many nodes are
    // present: a branch on row 0 and another on row 2 is two rows in the lane's
    // terms only if row 1 is empty, which the lane cannot express. Counting to
    // the highest keeps the projection monotonic, and the canvas is where
    // arbitrary row placement becomes drawable.
    int highest = -1;

    for (const auto& node : graph.getNodes())
        if (isVisible(node) && node.col == column)
            highest = juce::jmax(highest, node.row);

    return highest + 1;
}

bool isStageSplit(const Graph& graph, int stage)
{
    return getNumRows(graph, stage) > 1;
}

std::vector<BlockInstance*> getBlocksInRow(const Graph& graph, int stage, int row)
{
    std::vector<BlockInstance*> result;

    const auto occupied = columns(graph);

    if (stage < 0 || stage >= static_cast<int>(occupied.size()))
        return result;

    const int column = occupied[static_cast<size_t>(stage)];

    for (const auto& node : graph.getNodes())
        if (isVisible(node) && node.col == column && node.row == row)
            result.push_back(node.block);

    return result;
}

std::optional<BlockPosition> findBlock(const Graph& graph, const juce::String& uid)
{
    const auto* node = graph.findNode(uid);

    if (node == nullptr || ! isVisible(*node))
        return std::nullopt;

    const auto occupied = columns(graph);
    const auto it = std::find(occupied.begin(), occupied.end(), node->col);

    if (it == occupied.end())
        return std::nullopt;

    BlockPosition position;
    position.stage = static_cast<int>(std::distance(occupied.begin(), it));
    position.row = node->row;
    position.index = 0; // one block per cell
    return position;
}

BlockInstance* getBlockByIndex(const Graph& graph, int index)
{
    if (index < 0)
        return nullptr;

    int seen = 0;

    for (int stage = 0; stage < getNumStages(graph); ++stage)
    {
        for (int row = 0; row < getNumRows(graph, stage); ++row)
        {
            for (auto* block : getBlocksInRow(graph, stage, row))
            {
                if (seen == index)
                    return block;

                ++seen;
            }
        }
    }

    return nullptr;
}

//==============================================================================
namespace
{
/// The visible block nearest to `column` on `row`, searching left or right.
/// Endpoints are the fallbacks: nothing to the left means the signal comes from
/// IN, nothing to the right means it goes to OUT.
juce::String neighbourOnRow(const Graph& graph, int column, int row, bool searchLeft)
{
    const GraphNode* best = nullptr;

    for (const auto& node : graph.getNodes())
    {
        if (! isVisible(node) || node.row != row)
            continue;

        if (searchLeft ? node.col >= column : node.col <= column)
            continue;

        if (best == nullptr
            || (searchLeft ? node.col > best->col : node.col < best->col))
            best = &node;
    }

    if (best != nullptr)
        return best->uid;

    return searchLeft ? kInputNodeUid : kOutputNodeUid;
}

bool cellOccupied(const Graph& graph, int column, int row)
{
    for (const auto& node : graph.getNodes())
        if (isVisible(node) && node.col == column && node.row == row)
            return true;

    return false;
}

/// Splices `uid` between its neighbours on its own row.
void spliceIntoRow(Graph& graph, const juce::String& uid)
{
    const auto* node = graph.findNode(uid);

    if (node == nullptr)
        return;

    const auto before = neighbourOnRow(graph, node->col, node->row, true);
    const auto after = neighbourOnRow(graph, node->col, node->row, false);

    // The wire that used to skip past this position is what the new block
    // replaces. Without removing it the signal would both pass through the new
    // block and bypass it, summing at the far end — quietly doubling the dry
    // signal, which is the kind of bug you hear before you see.
    GraphWire skipped;
    skipped.fromUid = before;
    skipped.toUid = after;
    graph.removeWire(skipped);

    GraphWire incoming;
    incoming.fromUid = before;
    incoming.toUid = uid;
    graph.addWire(incoming);

    GraphWire outgoing;
    outgoing.fromUid = uid;
    outgoing.toUid = after;
    graph.addWire(outgoing);
}

/// Makes room at `column` by shifting everything at or past it one to the right.
void openColumn(Graph& graph, int column)
{
    for (const auto& node : graph.getNodes())
    {
        if (node.uid == kInputNodeUid)
            continue;

        if (auto* mutableNode = graph.findNode(node.uid))
            if (mutableNode->col >= column && ! mutableNode->isEndpoint())
                ++mutableNode->col;
    }

    // OUT stays to the right of everything.
    if (auto* out = graph.findNode(kOutputNodeUid))
    {
        int rightmost = 0;

        for (const auto& node : graph.getNodes())
            if (! node.isEndpoint())
                rightmost = juce::jmax(rightmost, node.col);

        out->col = rightmost + 1;
    }
}

/// Resolves a lane position to a grid column, opening one if needed.
int columnFor(Graph& graph, BlockPosition position)
{
    const auto occupied = columns(graph);

    if (occupied.empty())
        return 1;

    if (position.stage >= static_cast<int>(occupied.size()))
        return occupied.back() + 1;

    const int existing = occupied[static_cast<size_t>(juce::jmax(0, position.stage))];

    if (position.newStage || cellOccupied(graph, existing, position.row))
    {
        openColumn(graph, existing);
        return existing;
    }

    return existing;
}
} // namespace

void insertBlock(Graph& graph, std::unique_ptr<BlockInstance> block, BlockPosition position)
{
    if (block == nullptr)
        return;

    const auto uid = block->getUid();
    const int column = columnFor(graph, position);

    graph.addBlockNode(std::move(block), column, juce::jmax(0, position.row));
    spliceIntoRow(graph, uid);
}

bool removeBlock(Graph& graph, const juce::String& uid)
{
    const auto* node = graph.findNode(uid);

    if (node == nullptr || node->block == nullptr || node->isEndpoint())
        return false;

    return graph.healAround(uid);
}

bool moveBlock(Graph& graph, const juce::String& uid, BlockPosition position)
{
    auto* node = graph.findNode(uid);

    if (node == nullptr || node->block == nullptr || node->isEndpoint())
        return false;

    // Take the block out of the path first, so the source and destination it
    // used to join are wired to each other before we work out where it lands.
    // Otherwise a move within the same row would splice the block back against
    // its own stale wires.
    auto owned = graph.releaseBlock(uid);
    graph.healAround(uid);

    if (owned == nullptr)
        return false;

    insertBlock(graph, std::move(owned), position);
    return true;
}

} // namespace graphlane
} // namespace blockrig
