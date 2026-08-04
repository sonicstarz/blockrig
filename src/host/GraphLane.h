#pragma once

#include <optional>
#include <vector>

#include "host/BlockChain.h"
#include "host/Graph.h"

namespace blockrig
{

/// Lane-shaped queries over a graph, so the existing lane UI can read a graph
/// without knowing one exists.
///
/// **This is transitional and G5 deletes it.** It exists so the engine can go
/// live before the canvas is built, rather than requiring the processor swap and
/// a whole new UI to land in the same commit.
///
/// The projection is the obvious one: a node's column is its stage, its row is
/// its row. That holds exactly for graphs produced by `migrate_1_to_2`, which
/// gives every block its own column and stacks parallel branches on rows — the
/// two models agree by construction. It degrades predictably for a free-form
/// graph the canvas will eventually let you draw: a block wired backwards up the
/// grid still appears at its column, so the lane shows position rather than
/// signal order. That is the honest limit of drawing a graph as a line, and it
/// is why the canvas replaces it rather than extending it.
///
/// Stages are the *occupied* columns in ascending order, so a gap in the grid
/// does not produce an empty stage the lane would draw as a hole.
namespace graphlane
{

/// Occupied columns, ascending. Stage N means `columns()[N]`.
std::vector<int> columns(const Graph& graph);

int getNumStages(const Graph& graph);

/// Rows occupied at this stage. Zero for an out-of-range stage.
int getNumRows(const Graph& graph, int stage);

bool isStageSplit(const Graph& graph, int stage);

/// The blocks at this cell. A graph holds one block per cell, so this is empty
/// or a single element — the vector shape is kept because that is what the lane
/// asks for.
std::vector<BlockInstance*> getBlocksInRow(const Graph& graph, int stage, int row);

/// Where a block sits in lane coordinates, or nullopt if it is not in the graph.
/// Tailing nodes are excluded: they are already deleted as far as the UI is
/// concerned.
std::optional<BlockPosition> findBlock(const Graph& graph, const juce::String& uid);

/// Nth block in lane order — across stages, then rows. Matches the lane's own
/// getBlockByIndex ordering so callers that walk by index are unaffected.
BlockInstance* getBlockByIndex(const Graph& graph, int index);

} // namespace graphlane
} // namespace blockrig
