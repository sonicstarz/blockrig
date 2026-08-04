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

//==============================================================================
// Edits. These are what BlockRigProcessor's insert/remove/move call once the
// graph is the live model; the UI above them is untouched, because it already
// goes through the processor rather than the chain.

/// Places a block at a lane position and splices it into the signal path.
///
/// The splice is along the node's row: the nearest block to its left on that row
/// becomes its source (IN if there is none), the nearest to its right becomes
/// its destination (OUT if there is none), and the wire that previously joined
/// those two is removed. Inserting into the middle of a chain therefore does
/// what the lane did — the signal now goes through the new block instead of
/// past it.
///
/// `position.newStage`, or a position whose cell is already occupied, inserts a
/// fresh column and shifts everything at or after it to the right, so no two
/// blocks ever share a cell.
void insertBlock(Graph& graph, std::unique_ptr<BlockInstance> block, BlockPosition position);

/// Removes a block and heals the gap, so the signal path closes up. Returns
/// false if the uid is not a block node.
bool removeBlock(Graph& graph, const juce::String& uid);

/// Moves a block to a new lane position, healing the gap it left and splicing it
/// in at the destination.
bool moveBlock(Graph& graph, const juce::String& uid, BlockPosition position);

} // namespace graphlane
} // namespace blockrig
