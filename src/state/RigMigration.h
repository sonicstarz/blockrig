#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace blockrig
{
namespace rigstate
{

/// Schema v2: the lane becomes a graph (docs/19 §3).
///
///   <Graph>
///     <Node uid="b1" col="1" row="0"> … the whole v1 <Block> subtree … </Node>
///     <Wire fromUid="__in" fromPort="0" toUid="b1" toPort="0"/>
///   </Graph>
///
/// Node carries the same attributes and children a v1 `<Block>` did — format,
/// identifier, uniqueId, the opaque `<State>`, the `<Editor>` memory — because
/// none of that is changing. What changes is the container: stages and rows are
/// replaced by explicit position and explicit connections.
namespace ids
{
inline constexpr const char* graph = "Graph";
inline constexpr const char* node = "Node";
inline constexpr const char* wire = "Wire";
inline constexpr const char* col = "col";
inline constexpr const char* nodeRow = "row";
inline constexpr const char* fromUid = "fromUid";
inline constexpr const char* fromPort = "fromPort";
inline constexpr const char* toUid = "toUid";
inline constexpr const char* toPort = "toPort";
} // namespace ids

/// Migrates a v1 rig document to v2 in place of the `<Lane>`, returning the new
/// tree. Pure: no processor, no plug-ins, no I/O — which is what makes it
/// fixture-testable, per the docs/14 migration policy.
///
/// The mapping:
///
///   - a linear chain becomes one row of nodes joined by consecutive wires;
///   - a split stage fans out from whatever preceded it into each row's chain,
///     and fans back in at whatever follows;
///   - a `dualMono` stage additionally gets a Utility node at the head of each
///     branch — sum-to-mono on, panned hard left for row 0 and hard right for
///     row 1 — because a bare fan-in sum would otherwise collapse a saved
///     dual-amp stereo rig into a centred mono-ish sum. That is a silent change
///     to how someone's rig sounds, which is not an acceptable migration.
///   - a `parallel` stage gets a Utility node only where the row carried
///     non-default gain or pan, since fan-in already sums at unity.
///
/// Rows are laid out on descending grid rows and columns advance along the
/// chain, so the migrated graph reads left-to-right exactly as the lane did.
///
/// Anything the reader does not recognise is carried through untouched, per the
/// schema's forward-compatibility rule.
juce::ValueTree migrate_1_to_2(const juce::ValueTree& rigV1);

/// Chains every migration needed to bring `rig` up to the current version.
/// Returns an invalid tree if the document is newer than this build knows, which
/// the caller reports as "made in a newer BlockRig".
juce::ValueTree migrateToCurrent(const juce::ValueTree& rig);

} // namespace rigstate
} // namespace blockrig
