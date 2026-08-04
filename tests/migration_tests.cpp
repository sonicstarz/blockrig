// Schema v1 -> v2 migration (docs/19 §G2, docs/14 migration policy). Pure
// ValueTree in, ValueTree out: no processor, no plug-ins, no files — so every
// fixture below is exact and the whole binary runs in milliseconds.
//
// The fixtures are the ones the plan names: empty rig, linear chain, one split
// stage, a split with uneven branch lengths, splits at the start and end of the
// chain, and — the one that matters most — a dualMono split, which must not
// quietly become a centred sum.

#include <cstdio>

#include "host/Graph.h"
#include "state/RigMigration.h"
#include "state/RigState.h"

namespace
{
int gFailures = 0;

void check(bool condition, const juce::String& what)
{
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what.toRawUTF8());
    if (! condition)
        ++gFailures;
}

using namespace blockrig::rigstate;

//==============================================================================
// Fixture builders, mirroring exactly what RigState::toValueTree writes today.

juce::ValueTree makeBlock(const juce::String& uid, const juce::String& name = "Plug")
{
    juce::ValueTree block(ids::block);
    block.setProperty(ids::blockUid, uid, nullptr);
    block.setProperty(ids::format, "VST3", nullptr);
    block.setProperty(ids::identifier, "/plugins/" + name + ".vst3", nullptr);
    block.setProperty(ids::uniqueId, 4242, nullptr);
    block.setProperty(ids::name, name, nullptr);
    block.setProperty(ids::manufacturer, "Acme", nullptr);
    block.setProperty(ids::bypassed, false, nullptr);

    juce::ValueTree state(ids::state);
    state.setProperty(ids::encoding, "base64", nullptr);
    state.setProperty("data", "AAAA" + uid, nullptr);
    block.appendChild(state, nullptr);

    juce::ValueTree editor("Editor");
    editor.setProperty("open", true, nullptr);
    editor.setProperty("x", 120, nullptr);
    block.appendChild(editor, nullptr);

    return block;
}

juce::ValueTree makeRow(const juce::StringArray& uids, float gainDb = 0.0f, float pan = 0.0f)
{
    juce::ValueTree row(ids::row);
    row.setProperty(ids::gainDb, gainDb, nullptr);
    row.setProperty("pan", pan, nullptr);

    for (const auto& uid : uids)
        row.appendChild(makeBlock(uid), nullptr);

    return row;
}

juce::ValueTree makeStage(const juce::Array<juce::ValueTree>& rows,
                          const juce::String& mode = "parallel")
{
    juce::ValueTree stage(ids::stage);
    stage.setProperty("mode", mode, nullptr);

    for (const auto& row : rows)
        stage.appendChild(row, nullptr);

    return stage;
}

juce::ValueTree makeRig(const juce::Array<juce::ValueTree>& stages)
{
    juce::ValueTree rig(ids::root);
    rig.setProperty(ids::schemaVersion, 1, nullptr);
    rig.setProperty(ids::name, "Test Rig", nullptr);
    rig.setProperty(ids::uuid, "abc-123", nullptr);

    juce::ValueTree input(ids::input);
    input.setProperty(ids::inputModeAttr, "mono", nullptr);
    rig.appendChild(input, nullptr);

    // Snapshots and mappings are uid-keyed and must survive untouched.
    juce::ValueTree snapshots("Snapshots");
    snapshots.setProperty("count", 3, nullptr);
    rig.appendChild(snapshots, nullptr);

    juce::ValueTree lane(ids::lane);

    for (const auto& stage : stages)
        lane.appendChild(stage, nullptr);

    rig.appendChild(lane, nullptr);
    return rig;
}

//==============================================================================
// Readers over the migrated graph.

juce::ValueTree graphOf(const juce::ValueTree& rig)
{
    return rig.getChildWithName(ids::graph);
}

juce::ValueTree nodeFor(const juce::ValueTree& graph, const juce::String& uid)
{
    for (const auto node : graph)
        if (node.hasType(ids::node) && node.getProperty(ids::blockUid).toString() == uid)
            return node;

    return {};
}

bool hasWire(const juce::ValueTree& graph, const juce::String& from, const juce::String& to)
{
    for (const auto wire : graph)
        if (wire.hasType(ids::wire)
            && wire.getProperty(ids::fromUid).toString() == from
            && wire.getProperty(ids::toUid).toString() == to)
            return true;

    return false;
}

int countNodes(const juce::ValueTree& graph)
{
    int count = 0;

    for (const auto child : graph)
        if (child.hasType(ids::node))
            ++count;

    return count;
}

/// Every Utility node the migration minted, in document order.
juce::Array<juce::ValueTree> utilityNodes(const juce::ValueTree& graph)
{
    juce::Array<juce::ValueTree> result;

    for (const auto node : graph)
        if (node.hasType(ids::node) && node.getProperty(ids::identifier).toString() == "utility")
            result.add(node);

    return result;
}

float paramOf(const juce::ValueTree& node, const juce::String& name)
{
    return static_cast<float>(node.getChildWithName("Parameters").getProperty(name, 0.0));
}

//==============================================================================
void testEmptyRig()
{
    std::printf("\nFixture: empty rig\n");

    const auto migrated = migrate_1_to_2(makeRig({}));
    const auto graph = graphOf(migrated);

    check(static_cast<int>(migrated.getProperty(ids::schemaVersion)) == 2, "schemaVersion bumps to 2");
    check(! migrated.getChildWithName(ids::lane).isValid(), "the Lane is gone");
    check(graph.isValid(), "a Graph replaced it");
    check(countNodes(graph) == 2, "only IN and OUT");
    check(hasWire(graph, "__in", "__out"), "IN feeds OUT directly");
}

void testLinearChain()
{
    std::printf("\nFixture: linear chain\n");

    const auto rig = makeRig({makeStage({makeRow({"b1"})}),
                              makeStage({makeRow({"b2"})}),
                              makeStage({makeRow({"b3"})})});

    const auto graph = graphOf(migrate_1_to_2(rig));

    check(countNodes(graph) == 5, "IN, three blocks, OUT");
    check(hasWire(graph, "__in", "b1"), "IN -> b1");
    check(hasWire(graph, "b1", "b2"), "b1 -> b2");
    check(hasWire(graph, "b2", "b3"), "b2 -> b3");
    check(hasWire(graph, "b3", "__out"), "b3 -> OUT");
    check(utilityNodes(graph).isEmpty(), "no Utility nodes are minted for a plain chain");

    // Columns advance along the chain, so the canvas reads like the lane did.
    check(static_cast<int>(nodeFor(graph, "b1").getProperty(ids::col)) == 1, "b1 in column 1");
    check(static_cast<int>(nodeFor(graph, "b3").getProperty(ids::col)) == 3, "b3 in column 3");
}

void testBlockSubtreeSurvives()
{
    std::printf("\nBlock identity and state survive intact\n");

    const auto graph = graphOf(migrate_1_to_2(makeRig({makeStage({makeRow({"b1"})})})));
    const auto node = nodeFor(graph, "b1");

    check(node.isValid(), "the block became a node");
    check(node.getProperty(ids::format).toString() == "VST3", "format kept");
    check(static_cast<int>(node.getProperty(ids::uniqueId)) == 4242, "uniqueId kept");
    check(node.getProperty(ids::identifier).toString().contains("Plug.vst3"), "identifier kept");

    const auto state = node.getChildWithName(ids::state);
    check(state.isValid() && state.getProperty("data").toString() == "AAAAb1",
          "the opaque state chunk is carried through byte for byte");

    check(node.getChildWithName("Editor").isValid(), "editor window memory kept");
}

void testUnrelatedChildrenSurvive()
{
    std::printf("\nEverything outside the Lane is untouched\n");

    const auto migrated = migrate_1_to_2(makeRig({makeStage({makeRow({"b1"})})}));

    check(migrated.getProperty(ids::name).toString() == "Test Rig", "rig name kept");
    check(migrated.getProperty(ids::uuid).toString() == "abc-123", "rig uuid kept");
    check(migrated.getChildWithName(ids::input).isValid(), "Input kept");
    check(static_cast<int>(migrated.getChildWithName("Snapshots").getProperty("count")) == 3,
          "Snapshots survive untouched - they are uid-keyed and uids did not change");
}

void testParallelSplit()
{
    std::printf("\nFixture: one parallel split stage\n");

    const auto rig = makeRig({makeStage({makeRow({"head"})}),
                              makeStage({makeRow({"a1"}), makeRow({"b1"})}, "parallel"),
                              makeStage({makeRow({"tail"})})});

    const auto graph = graphOf(migrate_1_to_2(rig));

    check(hasWire(graph, "head", "a1"), "fan-out: head -> a1");
    check(hasWire(graph, "head", "b1"), "fan-out: head -> b1");
    check(hasWire(graph, "a1", "tail"), "fan-in: a1 -> tail");
    check(hasWire(graph, "b1", "tail"), "fan-in: b1 -> tail");

    check(utilityNodes(graph).isEmpty(),
          "a parallel split with default gain and pan needs no Utility - fan-in already sums at unity");

    // Rows stack, so the branches are visually parallel.
    check(static_cast<int>(nodeFor(graph, "a1").getProperty(ids::nodeRow)) == 0, "a1 on row 0");
    check(static_cast<int>(nodeFor(graph, "b1").getProperty(ids::nodeRow)) == 1, "b1 on row 1");
}

void testParallelSplitWithRowGain()
{
    std::printf("\nA parallel row carrying gain or pan keeps it\n");

    const auto rig = makeRig({makeStage({makeRow({"head"})}),
                              makeStage({makeRow({"a1"}, -6.0f, 0.0f),
                                         makeRow({"b1"}, 0.0f, 0.5f)},
                                        "parallel")});

    const auto graph = graphOf(migrate_1_to_2(rig));
    const auto utilities = utilityNodes(graph);

    check(utilities.size() == 2, "one Utility per row that carried settings");

    if (utilities.size() == 2)
    {
        check(std::abs(paramOf(utilities[0], "gain") + 6.0f) < 1.0e-4f, "row 0's -6 dB is carried");
        check(std::abs(paramOf(utilities[1], "pan") - 0.5f) < 1.0e-4f, "row 1's pan is carried");
        check(paramOf(utilities[0], "sumToMono") < 0.5f, "parallel rows are not summed to mono");
    }

    // The Utility sits at the head of its branch, before the row's blocks.
    const auto firstUtilityUid = utilities.isEmpty()
                                   ? juce::String()
                                   : utilities[0].getProperty(ids::blockUid).toString();

    check(hasWire(graph, "head", firstUtilityUid), "head feeds the Utility");
    check(hasWire(graph, firstUtilityUid, "a1"), "the Utility feeds the row's first block");
}

void testDualMonoSplit()
{
    std::printf("\nFixture: dualMono split - the one that must not change how a rig sounds\n");

    const auto rig = makeRig({makeStage({makeRow({"head"})}),
                              makeStage({makeRow({"ampL"}), makeRow({"ampR"})}, "dualMono")});

    const auto graph = graphOf(migrate_1_to_2(rig));
    const auto utilities = utilityNodes(graph);

    check(utilities.size() == 2, "a Utility is minted for each side");

    if (utilities.size() == 2)
    {
        check(paramOf(utilities[0], "sumToMono") > 0.5f, "left branch sums to mono");
        check(paramOf(utilities[1], "sumToMono") > 0.5f, "right branch sums to mono");
        check(std::abs(paramOf(utilities[0], "pan") + 1.0f) < 1.0e-4f, "row 0 is panned hard left");
        check(std::abs(paramOf(utilities[1], "pan") - 1.0f) < 1.0e-4f, "row 1 is panned hard right");

        // Signal order: head -> Utility -> amp, on each side.
        const auto leftUtil = utilities[0].getProperty(ids::blockUid).toString();
        const auto rightUtil = utilities[1].getProperty(ids::blockUid).toString();

        check(leftUtil != rightUtil, "the two minted uids are distinct");
        check(hasWire(graph, "head", leftUtil) && hasWire(graph, leftUtil, "ampL"),
              "head -> Utility -> ampL");
        check(hasWire(graph, "head", rightUtil) && hasWire(graph, rightUtil, "ampR"),
              "head -> Utility -> ampR");
        check(hasWire(graph, "ampL", "__out") && hasWire(graph, "ampR", "__out"),
              "both amps fan in at OUT");
    }
}

void testUnevenBranchLengths()
{
    std::printf("\nFixture: split with uneven branch lengths\n");

    const auto rig = makeRig({makeStage({makeRow({"head"})}),
                              makeStage({makeRow({"x1", "x2", "x3"}), makeRow({"y1"})}, "parallel"),
                              makeStage({makeRow({"tail"})})});

    const auto graph = graphOf(migrate_1_to_2(rig));

    check(hasWire(graph, "head", "x1") && hasWire(graph, "head", "y1"), "both branches fan out");
    check(hasWire(graph, "x1", "x2") && hasWire(graph, "x2", "x3"), "the long branch stays in order");
    check(hasWire(graph, "x3", "tail"), "the long branch rejoins");
    check(hasWire(graph, "y1", "tail"), "the short branch rejoins at the same node");

    // The stage after an uneven split starts past the longest branch, so nothing
    // overlaps on the grid.
    const int tailColumn = static_cast<int>(nodeFor(graph, "tail").getProperty(ids::col));
    const int x3Column = static_cast<int>(nodeFor(graph, "x3").getProperty(ids::col));

    check(tailColumn > x3Column, "the rejoining block sits past the longest branch");
}

void testSplitAtChainStartAndEnd()
{
    std::printf("\nFixture: split at the start and at the end of the chain\n");

    // Split first: IN itself is the fan-out point.
    const auto atStart = graphOf(migrate_1_to_2(
        makeRig({makeStage({makeRow({"a1"}), makeRow({"b1"})}, "parallel"),
                 makeStage({makeRow({"tail"})})})));

    check(hasWire(atStart, "__in", "a1") && hasWire(atStart, "__in", "b1"),
          "IN fans out when the chain opens with a split");
    check(hasWire(atStart, "a1", "tail") && hasWire(atStart, "b1", "tail"), "and rejoins at tail");

    // Split last: OUT is the fan-in point.
    const auto atEnd = graphOf(migrate_1_to_2(
        makeRig({makeStage({makeRow({"head"})}),
                 makeStage({makeRow({"a1"}), makeRow({"b1"})}, "parallel")})));

    check(hasWire(atEnd, "head", "a1") && hasWire(atEnd, "head", "b1"), "head fans out");
    check(hasWire(atEnd, "a1", "__out") && hasWire(atEnd, "b1", "__out"),
          "both branches fan in at OUT when the chain ends split");

    // A rig that is nothing but a split.
    const auto only = graphOf(migrate_1_to_2(
        makeRig({makeStage({makeRow({"a1"}), makeRow({"b1"})}, "parallel")})));

    check(hasWire(only, "__in", "a1") && hasWire(only, "a1", "__out"), "IN -> a1 -> OUT");
    check(hasWire(only, "__in", "b1") && hasWire(only, "b1", "__out"), "IN -> b1 -> OUT");
}

void testEmptyRowPassesThrough()
{
    std::printf("\nAn empty row does not swallow its branch\n");

    juce::ValueTree emptyRow(ids::row);
    const auto rig = makeRig({makeStage({makeRow({"head"})}),
                              makeStage({makeRow({"a1"}), emptyRow}, "parallel"),
                              makeStage({makeRow({"tail"})})});

    const auto graph = graphOf(migrate_1_to_2(rig));

    check(hasWire(graph, "a1", "tail"), "the populated branch rejoins");
    check(hasWire(graph, "head", "tail"),
          "the empty branch becomes a direct wire rather than vanishing");
}

/// Builds a live Graph from a migrated document, which is the real proof that
/// the two halves agree: a migration that produced a cycle, a dangling wire or
/// an orphaned block would be caught here rather than in the app.
blockrig::Graph buildGraph(const juce::ValueTree& graphTree, int& wiresRejected)
{
    blockrig::Graph graph;
    wiresRejected = 0;

    for (const auto child : graphTree)
    {
        if (! child.hasType(ids::node))
            continue;

        const auto uid = child.getProperty(ids::blockUid).toString();

        if (uid == blockrig::kInputNodeUid || uid == blockrig::kOutputNodeUid)
            continue; // the Graph mints its own endpoints

        blockrig::GraphNode node;
        node.uid = uid;
        node.col = static_cast<int>(child.getProperty(ids::col, 0));
        node.row = static_cast<int>(child.getProperty(ids::nodeRow, 0));
        graph.addNode(node);
    }

    for (const auto child : graphTree)
    {
        if (! child.hasType(ids::wire))
            continue;

        blockrig::GraphWire wire;
        wire.fromUid = child.getProperty(ids::fromUid).toString();
        wire.toUid = child.getProperty(ids::toUid).toString();
        wire.fromPort = static_cast<int>(child.getProperty(ids::fromPort, 0));
        wire.toPort = static_cast<int>(child.getProperty(ids::toPort, 0));

        if (! graph.addWire(wire))
            ++wiresRejected;
    }

    return graph;
}

void testMigratedGraphsLoad()
{
    std::printf("\nMigrated rigs load into the engine and compile cleanly\n");

    struct Fixture
    {
        const char* name;
        juce::ValueTree rig;
    };

    juce::ValueTree emptyRow(ids::row);

    const Fixture fixtures[] = {
        {"empty", makeRig({})},
        {"linear", makeRig({makeStage({makeRow({"b1"})}), makeStage({makeRow({"b2"})})})},
        {"parallel split", makeRig({makeStage({makeRow({"head"})}),
                                    makeStage({makeRow({"a1"}), makeRow({"b1"})}, "parallel"),
                                    makeStage({makeRow({"tail"})})})},
        {"dualMono split", makeRig({makeStage({makeRow({"head"})}),
                                    makeStage({makeRow({"ampL"}), makeRow({"ampR"})}, "dualMono")})},
        {"uneven branches", makeRig({makeStage({makeRow({"head"})}),
                                     makeStage({makeRow({"x1", "x2", "x3"}), makeRow({"y1"})},
                                               "parallel"),
                                     makeStage({makeRow({"tail"})})})},
        {"split at start", makeRig({makeStage({makeRow({"a1"}), makeRow({"b1"})}, "parallel")})},
        {"row gain and pan", makeRig({makeStage({makeRow({"head"})}),
                                      makeStage({makeRow({"a1"}, -6.0f, 0.0f),
                                                 makeRow({"b1"}, 0.0f, 0.5f)},
                                                "parallel")})},
    };

    for (const auto& fixture : fixtures)
    {
        const auto graphTree = graphOf(migrate_1_to_2(fixture.rig));

        int rejected = 0;
        auto graph = buildGraph(graphTree, rejected);

        const auto order = graph.topologicalOrder();
        const auto plan = graph.compile(128);

        const juce::String label(fixture.name);

        check(rejected == 0, label + ": every wire is accepted (no cycles, no dangling ends)");
        check(order.has_value(), label + ": the migrated graph is a DAG");
        check(plan.dormantUids.empty(), label + ": nothing is left dormant");

        // Every block in the source rig must still be rendered.
        int blocksInRig = 0;

        for (const auto stage : fixture.rig.getChildWithName(ids::lane))
            for (const auto row : stage)
                for (const auto block : row)
                    if (block.hasType(ids::block))
                        ++blocksInRig;

        int blocksInPlan = 0;

        for (const auto& step : plan.steps)
            if (step.uid != blockrig::kInputNodeUid && step.uid != blockrig::kOutputNodeUid
                && ! step.uid.startsWith("mig_"))
                ++blocksInPlan;

        check(blocksInPlan == blocksInRig,
              label + ": all " + juce::String(blocksInRig) + " blocks survive into the plan");
    }
}

void testMigrateToCurrent()
{
    std::printf("\nVersion chaining\n");

    const auto v1 = makeRig({makeStage({makeRow({"b1"})})});
    const auto migrated = migrateToCurrent(v1);

    check(static_cast<int>(migrated.getProperty(ids::schemaVersion)) == 2, "v1 migrates to v2");

    // Already current: unchanged, and idempotent.
    const auto twice = migrateToCurrent(migrated);
    check(static_cast<int>(twice.getProperty(ids::schemaVersion)) == 2, "v2 stays v2");
    check(countNodes(graphOf(twice)) == countNodes(graphOf(migrated)),
          "migrating an already-current rig changes nothing");

    // Newer than we know: refused, so the caller can say so plainly.
    auto future = v1.createCopy();
    future.setProperty(ids::schemaVersion, 99, nullptr);
    check(! migrateToCurrent(future).isValid(), "a newer document is refused rather than mangled");
}

} // namespace

int main()
{
    std::printf("Rig schema migration tests (G2)\n");

    testEmptyRig();
    testLinearChain();
    testBlockSubtreeSurvives();
    testUnrelatedChildrenSurvive();
    testParallelSplit();
    testParallelSplitWithRowGain();
    testDualMonoSplit();
    testUnevenBranchLengths();
    testSplitAtChainStartAndEnd();
    testEmptyRowPassesThrough();
    testMigratedGraphsLoad();
    testMigrateToCurrent();

    std::printf("\n%s (%d failure%s)\n",
                gFailures == 0 ? "ALL PASSED" : "FAILURES",
                gFailures,
                gFailures == 1 ? "" : "s");

    return gFailures == 0 ? 0 : 1;
}
