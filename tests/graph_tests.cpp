// Graph engine tests (docs/19 §G1). The compiler is deliberately pure — node
// latency lives on the node, refreshed from the block on the message thread —
// so topology, dormancy, buffer reuse and the latency-alignment maths are all
// testable without instantiating a plug-in. Audio equality against real hosted
// plug-ins arrives with the renderer; these are the guarantees underneath it.

#include <cstdio>
#include <set>

#include "host/Graph.h"

namespace
{
int gFailures = 0;

void check(bool condition, const juce::String& what)
{
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what.toRawUTF8());
    if (! condition)
        ++gFailures;
}

using namespace blockrig;

/// A node with no block, standing in for a plug-in that reports `latency`.
GraphNode makeNode(const juce::String& uid, int col, int row, int latency = 0)
{
    GraphNode node;
    node.uid = uid;
    node.col = col;
    node.row = row;
    node.latencySamples = latency;
    return node;
}

void wire(Graph& graph, const juce::String& from, const juce::String& to)
{
    GraphWire w;
    w.fromUid = from;
    w.toUid = to;
    graph.addWire(w);
}

const PlanStep* stepFor(const RenderPlan& plan, const juce::String& uid)
{
    for (const auto& step : plan.steps)
        if (step.uid == uid)
            return &step;

    return nullptr;
}

int indexOf(const RenderPlan& plan, const juce::String& uid)
{
    for (size_t i = 0; i < plan.steps.size(); ++i)
        if (plan.steps[i].uid == uid)
            return static_cast<int>(i);

    return -1;
}

//==============================================================================
void testTopologyAndCycles()
{
    std::printf("\nTopology and cycle rejection\n");

    Graph graph;
    graph.addNode(makeNode("a", 1, 0));
    graph.addNode(makeNode("b", 2, 0));

    wire(graph, kInputNodeUid, "a");
    wire(graph, "a", "b");
    wire(graph, "b", kOutputNodeUid);

    const auto order = graph.topologicalOrder();
    check(order.has_value(), "linear chain sorts");

    const auto plan = graph.compile(128);
    check(indexOf(plan, kInputNodeUid) < indexOf(plan, "a"), "IN runs before a");
    check(indexOf(plan, "a") < indexOf(plan, "b"), "a runs before b");
    check(indexOf(plan, "b") < indexOf(plan, kOutputNodeUid), "b runs before OUT");

    // The cycle rule is enforced at the gesture, so the wire never lands.
    GraphWire backwards;
    backwards.fromUid = "b";
    backwards.toUid = "a";
    check(graph.canAddWire(backwards) == WireRejection::wouldCycle, "b -> a refused as a cycle");
    check(! graph.addWire(backwards), "cycle wire is not added");

    GraphWire self;
    self.fromUid = "a";
    self.toUid = "a";
    check(graph.canAddWire(self) == WireRejection::selfConnection, "self-connection refused");

    GraphWire duplicate;
    duplicate.fromUid = "a";
    duplicate.toUid = "b";
    check(graph.canAddWire(duplicate) == WireRejection::duplicate, "duplicate wire refused");

    GraphWire intoInput;
    intoInput.fromUid = "a";
    intoInput.toUid = kInputNodeUid;
    check(graph.canAddWire(intoInput) == WireRejection::selfConnection, "nothing may feed IN");

    GraphWire outOfOutput;
    outOfOutput.fromUid = kOutputNodeUid;
    outOfOutput.toUid = "a";
    check(graph.canAddWire(outOfOutput) == WireRejection::selfConnection, "OUT may not feed");
}

//==============================================================================
void testFanOutAndFanIn()
{
    std::printf("\nFan-out and fan-in\n");

    // IN -> a -> {b, c} -> OUT. Fan-out is the splitter; fan-in sums.
    Graph graph;
    graph.addNode(makeNode("a", 1, 0));
    graph.addNode(makeNode("b", 2, 0));
    graph.addNode(makeNode("c", 2, 1));

    wire(graph, kInputNodeUid, "a");
    wire(graph, "a", "b");
    wire(graph, "a", "c");
    wire(graph, "b", kOutputNodeUid);
    wire(graph, "c", kOutputNodeUid);

    const auto plan = graph.compile(128);

    check(plan.steps.size() == 5, "all five nodes are scheduled");

    const auto* out = stepFor(plan, kOutputNodeUid);
    check(out != nullptr && out->inputs.size() == 2, "OUT sums two branches");

    const auto* a = stepFor(plan, "a");
    check(a != nullptr && a->inputs.size() == 1, "a takes one input");

    // Both branches must run before the node that sums them.
    check(indexOf(plan, "b") < indexOf(plan, kOutputNodeUid), "b runs before OUT");
    check(indexOf(plan, "c") < indexOf(plan, kOutputNodeUid), "c runs before OUT");

    // Fan-out feeds two consumers from one buffer, so a's buffer cannot be
    // recycled until both have run.
    const auto* b = stepFor(plan, "b");
    const auto* c = stepFor(plan, "c");
    check(b != nullptr && c != nullptr && b->inputs[0].bufferIndex == c->inputs[0].bufferIndex,
          "both branches read a's single output buffer");
}

//==============================================================================
void testLatencyAlignment()
{
    std::printf("\nLatency alignment at fan-in\n");

    // Two parallel branches with unequal plug-in latency, summed at OUT. The
    // shorter branch must be delayed by the difference or the sum combs.
    Graph graph;
    graph.addNode(makeNode("split", 1, 0, 0));
    graph.addNode(makeNode("slow", 2, 0, 256));
    graph.addNode(makeNode("fast", 2, 1, 64));

    wire(graph, kInputNodeUid, "split");
    wire(graph, "split", "slow");
    wire(graph, "split", "fast");
    wire(graph, "slow", kOutputNodeUid);
    wire(graph, "fast", kOutputNodeUid);

    const auto plan = graph.compile(128);

    check(plan.totalLatencySamples == 256, "graph latency is the longest path, not the sum");

    const auto* out = stepFor(plan, kOutputNodeUid);
    check(out != nullptr && out->inputs.size() == 2, "OUT has both branches");

    // Exactly one branch is delayed, by exactly the difference.
    int delayed = 0;
    int delaySamples = 0;

    if (out != nullptr)
    {
        for (const auto& input : out->inputs)
        {
            if (input.delayIndex >= 0)
            {
                ++delayed;
                delaySamples = plan.delays[static_cast<size_t>(input.delayIndex)].lengthSamples;
            }
        }
    }

    check(delayed == 1, "exactly one branch is delayed");
    check(delaySamples == 192, "the fast branch is delayed by 256 - 64 = 192 samples");
    check(plan.delays.size() == 1, "one delay line allocated");

    if (! plan.delays.empty())
        check(plan.delays[0].buffer.getNumSamples() >= 192 + 128,
              "delay line is sized for its length plus a block");

    // Equal latency needs no delay at all.
    Graph even;
    even.addNode(makeNode("split", 1, 0, 0));
    even.addNode(makeNode("l", 2, 0, 128));
    even.addNode(makeNode("r", 2, 1, 128));
    wire(even, kInputNodeUid, "split");
    wire(even, "split", "l");
    wire(even, "split", "r");
    wire(even, "l", kOutputNodeUid);
    wire(even, "r", kOutputNodeUid);

    const auto evenPlan = even.compile(128);
    check(evenPlan.delays.empty(), "matched branches allocate no delay lines");
    check(evenPlan.totalLatencySamples == 128, "matched branches report 128");

    // Uneven branch *lengths*, not just per-block latency: three blocks of 100
    // against one of 50 means the short side waits 250.
    Graph uneven;
    uneven.addNode(makeNode("split", 1, 0, 0));
    uneven.addNode(makeNode("x1", 2, 0, 100));
    uneven.addNode(makeNode("x2", 3, 0, 100));
    uneven.addNode(makeNode("x3", 4, 0, 100));
    uneven.addNode(makeNode("y1", 2, 1, 50));
    wire(uneven, kInputNodeUid, "split");
    wire(uneven, "split", "x1");
    wire(uneven, "x1", "x2");
    wire(uneven, "x2", "x3");
    wire(uneven, "x3", kOutputNodeUid);
    wire(uneven, "split", "y1");
    wire(uneven, "y1", kOutputNodeUid);

    const auto unevenPlan = uneven.compile(128);
    check(unevenPlan.totalLatencySamples == 300, "long branch sums to 300");
    check(unevenPlan.delays.size() == 1 && unevenPlan.delays[0].lengthSamples == 250,
          "short branch is delayed 300 - 50 = 250");
}

//==============================================================================
void testDormantNodes()
{
    std::printf("\nDormant nodes\n");

    Graph graph;
    graph.addNode(makeNode("live", 1, 0));
    graph.addNode(makeNode("parked", 1, 2));
    graph.addNode(makeNode("orphanFed", 1, 3));

    wire(graph, kInputNodeUid, "live");
    wire(graph, "live", kOutputNodeUid);

    // Fed by IN but reaching nothing: still dormant, because it cannot reach OUT.
    wire(graph, kInputNodeUid, "orphanFed");

    const auto plan = graph.compile(128);

    const std::set<juce::String> dormant(plan.dormantUids.begin(), plan.dormantUids.end());

    check(dormant.count("parked") == 1, "an unwired block is dormant");
    check(dormant.count("orphanFed") == 1, "a block that cannot reach OUT is dormant");
    check(dormant.count("live") == 0, "a wired block is not dormant");
    check(stepFor(plan, "parked") == nullptr, "dormant nodes cost nothing at render time");
    check(plan.steps.size() == 3, "only IN, live and OUT are scheduled");
}

//==============================================================================
void testBufferReuse()
{
    std::printf("\nBuffer pool reuse\n");

    // A long serial chain should not need one buffer per node: each block's
    // input buffer is free the moment the block has run.
    Graph graph;
    juce::String previous = kInputNodeUid;

    for (int i = 0; i < 12; ++i)
    {
        const auto uid = "n" + juce::String(i);
        graph.addNode(makeNode(uid, i + 1, 0));
        wire(graph, previous, uid);
        previous = uid;
    }

    wire(graph, previous, kOutputNodeUid);

    const auto plan = graph.compile(128);

    check(plan.steps.size() == 14, "fourteen nodes scheduled");
    check(plan.numBuffers <= 3, "a serial chain of 12 reuses at most 3 buffers");
    check(plan.numBuffers >= 2, "and needs at least 2, since output never aliases input");
}

//==============================================================================
void testHealAndRemove()
{
    std::printf("\nRemoval and healing\n");

    Graph graph;
    graph.addNode(makeNode("a", 1, 0));
    graph.addNode(makeNode("b", 2, 0));
    graph.addNode(makeNode("c", 3, 0));

    wire(graph, kInputNodeUid, "a");
    wire(graph, "a", "b");
    wire(graph, "b", "c");
    wire(graph, "c", kOutputNodeUid);

    check(graph.healAround("b"), "b is pulled out of the chain");
    check(graph.findNode("b") == nullptr, "b is gone");

    const auto plan = graph.compile(128);
    check(plan.dormantUids.empty(), "the chain healed, so nothing is dormant");
    check(indexOf(plan, "a") < indexOf(plan, "c"), "a now feeds c directly");
    check(plan.steps.size() == 4, "IN, a, c, OUT");

    check(! graph.removeNode(kInputNodeUid), "IN cannot be removed");
    check(! graph.removeNode(kOutputNodeUid), "OUT cannot be removed");

    // Plain removal leaves a hole rather than healing.
    Graph other;
    other.addNode(makeNode("x", 1, 0));
    other.addNode(makeNode("y", 2, 0));
    wire(other, kInputNodeUid, "x");
    wire(other, "x", "y");
    wire(other, "y", kOutputNodeUid);

    check(other.removeNode("x"), "x removed");
    check(other.wiresAt("x").empty(), "its wires went with it");

    const auto holed = other.compile(128);
    const std::set<juce::String> dormant(holed.dormantUids.begin(), holed.dormantUids.end());
    check(dormant.count("y") == 1, "y is orphaned by a non-healing removal");
}

//==============================================================================
void testDiamond()
{
    std::printf("\nDiamond: fan-out and fan-in around a shared tail\n");

    // IN -> a -> {b, c} -> d -> OUT, with b and c unequal. The alignment has to
    // happen at d, not at OUT, and d's own latency lands downstream of it.
    Graph graph;
    graph.addNode(makeNode("a", 1, 0, 10));
    graph.addNode(makeNode("b", 2, 0, 200));
    graph.addNode(makeNode("c", 2, 1, 40));
    graph.addNode(makeNode("d", 3, 0, 5));

    wire(graph, kInputNodeUid, "a");
    wire(graph, "a", "b");
    wire(graph, "a", "c");
    wire(graph, "b", "d");
    wire(graph, "c", "d");
    wire(graph, "d", kOutputNodeUid);

    const auto plan = graph.compile(128);

    check(plan.totalLatencySamples == 10 + 200 + 5, "10 + 200 + 5 = 215 down the long path");

    const auto* d = stepFor(plan, "d");
    check(d != nullptr && d->inputs.size() == 2, "d sums both branches");
    check(plan.delays.size() == 1, "one alignment delay, at d");
    check(! plan.delays.empty() && plan.delays[0].lengthSamples == 160,
          "the c branch is delayed by 200 - 40 = 160");

    check(indexOf(plan, "b") < indexOf(plan, "d"), "b before d");
    check(indexOf(plan, "c") < indexOf(plan, "d"), "c before d");
}

} // namespace

int main()
{
    std::printf("Graph engine tests (G1)\n");

    testTopologyAndCycles();
    testFanOutAndFanIn();
    testLatencyAlignment();
    testDormantNodes();
    testBufferReuse();
    testHealAndRemove();
    testDiamond();

    std::printf("\n%s (%d failure%s)\n",
                gFailures == 0 ? "ALL PASSED" : "FAILURES",
                gFailures,
                gFailures == 1 ? "" : "s");

    return gFailures == 0 ? 0 : 1;
}
