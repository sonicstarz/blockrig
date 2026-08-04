#include "state/RigMigration.h"

#include <cmath>

#include "state/RigState.h"

namespace blockrig
{
namespace rigstate
{
namespace
{
/// Uids of the endpoint nodes. Must match host/Graph.h — they are written into
/// the document, so they are part of the schema.
constexpr const char* kInUid = "__in";
constexpr const char* kOutUid = "__out";

/// Utility blocks minted by the migration need uids that cannot collide with
/// anything already in the rig.
juce::String mintUid(const juce::ValueTree& graph, const juce::String& hint)
{
    auto candidate = "mig_" + hint;
    int suffix = 0;

    const auto taken = [&graph](const juce::String& uid)
    {
        for (const auto node : graph)
            if (node.getProperty(ids::blockUid).toString() == uid)
                return true;

        return false;
    };

    while (taken(candidate))
        candidate = "mig_" + hint + "_" + juce::String(++suffix);

    return candidate;
}

void addWire(juce::ValueTree& graph, const juce::String& from, const juce::String& to)
{
    juce::ValueTree wire(ids::wire);
    wire.setProperty(ids::fromUid, from, nullptr);
    wire.setProperty(ids::fromPort, 0, nullptr);
    wire.setProperty(ids::toUid, to, nullptr);
    wire.setProperty(ids::toPort, 0, nullptr);
    graph.appendChild(wire, nullptr);
}

/// A Utility node standing in for a row's gain/pan, and for the mono-summing
/// half of a dualMono row.
juce::ValueTree makeUtilityNode(const juce::String& uid, int col, int row,
                                float gainDb, float pan, bool sumToMono)
{
    juce::ValueTree node(ids::node);
    node.setProperty(ids::blockUid, uid, nullptr);
    node.setProperty(ids::col, col, nullptr);
    node.setProperty(ids::nodeRow, row, nullptr);

    // Identity matches UtilityBlockProcessor::getBlockDescription().
    node.setProperty(ids::format, "BlockRig", nullptr);
    node.setProperty(ids::identifier, "utility", nullptr);
    node.setProperty(ids::uniqueId, 0x55544C31, nullptr);
    node.setProperty(ids::name, "Utility", nullptr);
    node.setProperty(ids::manufacturer, "BlockRig", nullptr);
    node.setProperty(ids::bypassed, false, nullptr);

    // Parameters are written as a plain child rather than an opaque <State>
    // chunk: this is a built-in block whose APVTS shape we own, and synthesising
    // a *third-party* chunk is what the schema forbids, not this.
    juce::ValueTree params("Parameters");
    params.setProperty("gain", gainDb, nullptr);
    params.setProperty("pan", pan, nullptr);
    params.setProperty("sumToMono", sumToMono, nullptr);
    node.appendChild(params, nullptr);

    return node;
}

/// Turns a v1 <Block> into a v2 <Node> at the given grid position, keeping every
/// attribute and child exactly as it was.
juce::ValueTree blockToNode(const juce::ValueTree& block, int col, int row)
{
    juce::ValueTree converted(ids::node);

    for (int i = 0; i < block.getNumProperties(); ++i)
    {
        const auto property = block.getPropertyName(i);
        converted.setProperty(property, block.getProperty(property), nullptr);
    }

    for (const auto child : block)
        converted.appendChild(child.createCopy(), nullptr);

    converted.setProperty(ids::col, col, nullptr);
    converted.setProperty(ids::nodeRow, row, nullptr);

    return converted;
}

bool isDefault(float value, float expected)
{
    return std::abs(value - expected) < 1.0e-4f;
}
} // namespace

juce::ValueTree migrate_1_to_2(const juce::ValueTree& rigV1)
{
    if (! rigV1.isValid())
        return {};

    auto rig = rigV1.createCopy();
    rig.setProperty(ids::schemaVersion, 2, nullptr);

    const auto lane = rig.getChildWithName(ids::lane);

    juce::ValueTree graph(ids::graph);

    juce::ValueTree inNode(ids::node);
    inNode.setProperty(ids::blockUid, kInUid, nullptr);
    inNode.setProperty(ids::col, 0, nullptr);
    inNode.setProperty(ids::nodeRow, 0, nullptr);
    graph.appendChild(inNode, nullptr);

    // Where the next stage's wires come from. Starts at IN, and after a split it
    // is the set of every branch's tail — which is exactly what makes the fan-in
    // fall out of the same loop as the linear case.
    juce::StringArray upstream{kInUid};
    int column = 1;

    if (lane.isValid())
    {
        for (const auto stage : lane)
        {
            if (! stage.hasType(ids::stage))
                continue;

            const bool dualMono = stage.getProperty("mode").toString() == "dualMono";
            const int numRows = stage.getNumChildren();

            juce::StringArray tails;
            int widestColumn = column;

            int rowIndex = 0;

            for (const auto row : stage)
            {
                if (! row.hasType(ids::row))
                    continue;

                const auto gainDb = static_cast<float>(row.getProperty(ids::gainDb, 0.0));
                const auto pan = static_cast<float>(row.getProperty("pan", 0.0));

                // A branch inherits the row's gain and pan, and a dualMono row
                // additionally has to be summed to mono and placed hard on its
                // own side — which is what the lane did implicitly and a bare
                // fan-in sum would silently discard.
                const bool needsUtility = dualMono
                                       || ! isDefault(gainDb, 0.0f)
                                       || ! isDefault(pan, 0.0f);

                juce::String previous = {};
                int col = column;

                if (needsUtility)
                {
                    const float utilityPan = dualMono ? (rowIndex == 0 ? -1.0f : 1.0f) : pan;
                    const auto uid = mintUid(graph, "util_" + juce::String(rowIndex)
                                                    + "_" + juce::String(col));

                    graph.appendChild(
                        makeUtilityNode(uid, col, rowIndex, gainDb, utilityPan, dualMono), nullptr);

                    for (const auto& source : upstream)
                        addWire(graph, source, uid);

                    previous = uid;
                    ++col;
                }

                for (const auto block : row)
                {
                    if (! block.hasType(ids::block))
                        continue;

                    const auto uid = block.getProperty(ids::blockUid).toString();
                    graph.appendChild(blockToNode(block, col, rowIndex), nullptr);

                    if (previous.isEmpty())
                    {
                        for (const auto& source : upstream)
                            addWire(graph, source, uid);
                    }
                    else
                    {
                        addWire(graph, previous, uid);
                    }

                    previous = uid;
                    ++col;
                }

                // An empty row passes its upstream straight through, so the
                // branch is not lost.
                if (previous.isNotEmpty())
                    tails.add(previous);
                else
                    tails.addArray(upstream);

                widestColumn = juce::jmax(widestColumn, col);
                ++rowIndex;
            }

            if (numRows > 0 && ! tails.isEmpty())
            {
                tails.removeDuplicates(false);
                upstream = tails;
            }

            column = widestColumn;
        }
    }

    juce::ValueTree outNode(ids::node);
    outNode.setProperty(ids::blockUid, kOutUid, nullptr);
    outNode.setProperty(ids::col, column, nullptr);
    outNode.setProperty(ids::nodeRow, 0, nullptr);
    graph.appendChild(outNode, nullptr);

    for (const auto& source : upstream)
        addWire(graph, source, kOutUid);

    if (lane.isValid())
        rig.removeChild(lane, nullptr);

    rig.appendChild(graph, nullptr);
    return rig;
}

juce::ValueTree migrateToCurrent(const juce::ValueTree& rig)
{
    if (! rig.isValid())
        return {};

    auto current = rig.createCopy();
    int version = static_cast<int>(current.getProperty(ids::schemaVersion, 0));

    if (version > 2)
        return {}; // made in a newer BlockRig

    if (version <= 1)
    {
        current = migrate_1_to_2(current);
        version = 2;
    }

    return current;
}

} // namespace rigstate
} // namespace blockrig
