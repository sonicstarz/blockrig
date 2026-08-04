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

} // namespace graphlane
} // namespace blockrig
