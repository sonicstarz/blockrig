// Graph engine tests (docs/19 §G1). The compiler is deliberately pure — node
// latency lives on the node, refreshed from the block on the message thread —
// so topology, dormancy, buffer reuse and the latency-alignment maths are all
// testable without instantiating a plug-in. Audio equality against real hosted
// plug-ins arrives with the renderer; these are the guarantees underneath it.

#include <cmath>
#include <cstdio>
#include <set>

#include "host/Graph.h"
#include "host/GraphEngine.h"
#include "host/GraphLane.h"

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

//==============================================================================
// Rendering. From here on the plan is actually executed, which needs blocks
// that behave like plug-ins.

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 128;

/// A plug-in that delays by exactly the latency it reports, and optionally
/// inverts. Both halves matter: a plug-in that reported latency without
/// delaying would make the null test pass for the wrong reason.
///
/// `reportedLatency` exists so a test can build the honest failure mode — a
/// plug-in that delays but under-reports, leaving the compiler nothing to
/// compensate. Pass -1 (the default) for a plug-in that tells the truth.
class DelayPlugin : public juce::AudioPluginInstance
{
public:
    DelayPlugin(int latencySamples, bool invert, int reportedLatency = -1)
        : juce::AudioPluginInstance(BusesProperties()
                                        .withInput("In", juce::AudioChannelSet::stereo(), true)
                                        .withOutput("Out", juce::AudioChannelSet::stereo(), true)),
          mLatency(latencySamples),
          mInvert(invert)
    {
        setLatencySamples(reportedLatency >= 0 ? reportedLatency : mLatency);
    }

    const juce::String getName() const override { return "DelayPlugin"; }

    void prepareToPlay(double, int maximumExpectedSamplesPerBlock) override
    {
        mLine.setSize(2, mLatency + maximumExpectedSamplesPerBlock + 1, false, true, true);
        mLine.clear();
        mWritePosition = 0;
    }

    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int numSamples = buffer.getNumSamples();
        const float sign = mInvert ? -1.0f : 1.0f;

        if (mLatency <= 0)
        {
            buffer.applyGain(sign);
            return;
        }

        const int capacity = mLine.getNumSamples();

        for (int channel = 0; channel < juce::jmin(2, buffer.getNumChannels()); ++channel)
        {
            auto* data = buffer.getWritePointer(channel);
            auto* line = mLine.getWritePointer(channel);
            int writePosition = mWritePosition;

            for (int i = 0; i < numSamples; ++i)
            {
                const int readPosition = (writePosition + capacity - mLatency) % capacity;
                const float delayed = line[readPosition];
                line[writePosition] = data[i];
                data[i] = delayed * sign;
                writePosition = (writePosition + 1) % capacity;
            }
        }

        mWritePosition = (mWritePosition + numSamples) % capacity;
    }

    void fillInPluginDescription(juce::PluginDescription& description) const override
    {
        description.name = "DelayPlugin";
        description.pluginFormatName = "Test";
        description.uniqueId = 1;
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    int mLatency = 0;
    bool mInvert = false;
    juce::AudioBuffer<float> mLine;
    int mWritePosition = 0;
};

/// Keeps test blocks alive for the duration of a test; the graph only holds
/// raw pointers, exactly as the lane does.
struct BlockPool
{
    std::vector<std::unique_ptr<BlockInstance>> blocks;

    BlockInstance* add(const juce::String& uid, int latency, bool invert = false,
                       int reportedLatency = -1)
    {
        auto plugin = std::make_unique<DelayPlugin>(latency, invert, reportedLatency);
        auto block = std::make_unique<BlockInstance>(std::move(plugin), uid);
        block->prepare(kSampleRate, kBlockSize, false);

        auto* raw = block.get();
        blocks.push_back(std::move(block));
        return raw;
    }
};

/// Deterministic, non-repeating enough to catch an alignment error that a sine
/// would hide by lining up with its own period.
float testSample(int index)
{
    return 0.25f * std::sin(index * 0.07f) + 0.15f * std::sin(index * 0.31f + 1.1f);
}

/// Runs `numBlocks` blocks of test signal through the engine, returning the peak
/// absolute output seen after `primeBlocks` blocks have gone by. Priming matters:
/// every delay line starts full of zeros, so the first samples out are silence
/// no matter what the alignment does.
float peakAfterPriming(GraphEngine& engine, int numBlocks, int primeBlocks)
{
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;

    float peak = 0.0f;
    int sampleIndex = 0;

    for (int block = 0; block < numBlocks; ++block)
    {
        for (int i = 0; i < kBlockSize; ++i)
        {
            const float value = testSample(sampleIndex++);
            buffer.setSample(0, i, value);
            buffer.setSample(1, i, value);
        }

        engine.process(buffer, midi);

        if (block >= primeBlocks)
            peak = juce::jmax(peak, buffer.getMagnitude(0, kBlockSize));
    }

    return peak;
}

void testRenderPassThrough()
{
    std::printf("\nRendering: signal reaches OUT\n");

    BlockPool pool;

    GraphEngine engine;
    auto& graph = engine.getGraph();

    auto node = makeNode("a", 1, 0);
    node.block = pool.add("a", 0);
    graph.addNode(node);

    wire(graph, kInputNodeUid, "a");
    wire(graph, "a", kOutputNodeUid);

    engine.prepare(kSampleRate, kBlockSize);

    const float peak = peakAfterPriming(engine, 8, 1);
    check(peak > 0.1f, "audio flows IN -> block -> OUT");
    check(engine.getLatencySamples() == 0, "a zero-latency chain reports zero");
}

void testRenderFanInSums()
{
    std::printf("\nRendering: fan-in sums\n");

    BlockPool pool;

    GraphEngine engine;
    auto& graph = engine.getGraph();

    for (int branch = 0; branch < 2; ++branch)
    {
        const auto uid = "branch" + juce::String(branch);
        auto node = makeNode(uid, 1, branch);
        node.block = pool.add(uid, 0);
        graph.addNode(node);
        wire(graph, kInputNodeUid, uid);
        wire(graph, uid, kOutputNodeUid);
    }

    engine.prepare(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;

    for (int i = 0; i < kBlockSize; ++i)
    {
        buffer.setSample(0, i, 0.25f);
        buffer.setSample(1, i, 0.25f);
    }

    engine.process(buffer, midi);

    // Two unity branches of the same signal sum to twice it. Unity-gain fan-in
    // is the decided behaviour (docs/19 §7) — the Utility block is where trim
    // lives, not the wire.
    check(std::abs(buffer.getSample(0, 64) - 0.5f) < 1.0e-5f, "two branches sum at unity");
}

void testNullOnAlignedBranches()
{
    std::printf("\nThe null test: unequal branch latency must cancel\n");

    // IN fans out to a 256-sample branch and a 64-sample branch, the second
    // inverted, summed at OUT. If the compiler delays the short branch by
    // exactly 192 samples, the two arrive sample-aligned and cancel to silence.
    // If alignment is wrong by even one sample, the residue is loud.
    BlockPool pool;

    GraphEngine engine;
    auto& graph = engine.getGraph();

    auto slow = makeNode("slow", 1, 0);
    slow.block = pool.add("slow", 256, false);
    graph.addNode(slow);

    auto fast = makeNode("fast", 1, 1);
    fast.block = pool.add("fast", 64, true);
    graph.addNode(fast);

    wire(graph, kInputNodeUid, "slow");
    wire(graph, kInputNodeUid, "fast");
    wire(graph, "slow", kOutputNodeUid);
    wire(graph, "fast", kOutputNodeUid);

    engine.prepare(kSampleRate, kBlockSize);

    check(engine.getLatencySamples() == 256, "the graph reports the long branch's latency");

    // 256 samples of latency is two blocks; prime four to be safe.
    const float residue = peakAfterPriming(engine, 32, 4);

    std::printf("        residual peak: %.9f\n", residue);
    check(residue < 1.0e-6f, "aligned branches cancel to silence");

    // The control: the same graph with alignment defeated must NOT null, or the
    // test above proves nothing about the compensation. Both blocks still delay
    // by 256 and 64 samples, but report 0, so the compiler has nothing to
    // compensate — which is precisely the real-world failure this test guards
    // against. No engine backdoor needed; the plug-ins simply under-report, the
    // way a badly behaved plug-in does.
    BlockPool controlPool;

    GraphEngine control;
    auto& controlGraph = control.getGraph();

    auto slowControl = makeNode("slow", 1, 0);
    slowControl.block = controlPool.add("slow", 256, false, 0);
    controlGraph.addNode(slowControl);

    auto fastControl = makeNode("fast", 1, 1);
    fastControl.block = controlPool.add("fast", 64, true, 0);
    controlGraph.addNode(fastControl);

    wire(controlGraph, kInputNodeUid, "slow");
    wire(controlGraph, kInputNodeUid, "fast");
    wire(controlGraph, "slow", kOutputNodeUid);
    wire(controlGraph, "fast", kOutputNodeUid);

    control.prepare(kSampleRate, kBlockSize);

    check(control.getLatencySamples() == 0, "the control reports no latency to compensate");

    const float uncompensated = peakAfterPriming(control, 32, 4);

    std::printf("        uncompensated peak: %.9f\n", uncompensated);
    check(uncompensated > 0.01f, "without alignment the same graph does not null");
}

void testPlanSwapWhileRendering()
{
    std::printf("\nPlan swap under continuous rendering\n");

    BlockPool pool;

    GraphEngine engine;
    auto& graph = engine.getGraph();

    auto a = makeNode("a", 1, 0);
    a.block = pool.add("a", 0);
    graph.addNode(a);
    wire(graph, kInputNodeUid, "a");
    wire(graph, "a", kOutputNodeUid);

    engine.prepare(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;

    // Insert a block into the chain, render, splice it out again, render — the
    // add/remove churn the canvas will produce, with audio running throughout.
    // The point is that publishing mid-render never faults and the retirement
    // queue stays drainable; the audio thread must never free a plan.
    float peak = 0.0f;
    int sampleIndex = 0;

    for (int round = 0; round < 24; ++round)
    {
        const auto uid = "spliced" + juce::String(round);

        // Splice a new block between a and OUT.
        auto node = makeNode(uid, 2, 0);
        node.block = pool.add(uid, 0);
        engine.getGraph().addNode(node);

        GraphWire toOut;
        toOut.fromUid = "a";
        toOut.toUid = kOutputNodeUid;
        engine.getGraph().removeWire(toOut);

        wire(engine.getGraph(), "a", uid);
        wire(engine.getGraph(), uid, kOutputNodeUid);
        engine.publish();

        for (int s = 0; s < kBlockSize; ++s)
            buffer.setSample(0, s, testSample(sampleIndex++));

        engine.process(buffer, midi);
        peak = juce::jmax(peak, buffer.getMagnitude(0, kBlockSize));

        // Pull it back out; the chain heals to a -> OUT.
        engine.getGraph().healAround(uid);
        engine.publish();

        for (int s = 0; s < kBlockSize; ++s)
            buffer.setSample(0, s, testSample(sampleIndex++));

        engine.process(buffer, midi);
        engine.collectGarbage();
    }

    check(peak > 0.05f, "audio keeps flowing across 24 splice/heal rounds");
    check(engine.getGraph().getNodes().size() == 3, "the graph is back to IN, a, OUT");
}

/// A BlockInstance that reports when it is destroyed, so ownership can be
/// asserted rather than assumed.
struct DeathWatch
{
    static int destroyed;
};

int DeathWatch::destroyed = 0;

class WatchedPlugin final : public DelayPlugin
{
public:
    WatchedPlugin() : DelayPlugin(0, false) {}
    ~WatchedPlugin() override { ++DeathWatch::destroyed; }
};

void testGraphOwnsItsBlocks()
{
    std::printf("\nThe graph owns its blocks\n");

    DeathWatch::destroyed = 0;

    {
        Graph graph;

        auto plugin = std::make_unique<WatchedPlugin>();
        auto block = std::make_unique<BlockInstance>(std::move(plugin), "owned");
        block->prepare(kSampleRate, kBlockSize, false);

        graph.addBlockNode(std::move(block), 1, 0);

        check(graph.getNumBlocks() == 1, "the block is counted");
        check(graph.getBlockByUid("owned") != nullptr, "and reachable by uid");
        check(graph.getBlocks().size() == 1, "and listed");
        check(graph.findNode("owned") != nullptr, "its node exists");
        check(DeathWatch::destroyed == 0, "nothing freed yet");

        // Removing the node frees the block it owns.
        graph.removeNode("owned");
        check(DeathWatch::destroyed == 1, "removing the node frees the block");
        check(graph.getNumBlocks() == 0, "and the count drops");
    }

    // Releasing hands ownership back out, so the block outlives the graph.
    DeathWatch::destroyed = 0;

    std::unique_ptr<BlockInstance> escaped;

    {
        Graph graph;

        auto plugin = std::make_unique<WatchedPlugin>();
        auto block = std::make_unique<BlockInstance>(std::move(plugin), "released");
        block->prepare(kSampleRate, kBlockSize, false);
        graph.addBlockNode(std::move(block), 1, 0);

        escaped = graph.releaseBlock("released");
        check(escaped != nullptr, "releaseBlock hands the block back");

        graph.removeNode("released");
        check(DeathWatch::destroyed == 0, "a released block is not freed by the graph");
    }

    check(DeathWatch::destroyed == 0, "nor when the graph goes away");
    escaped.reset();
    check(DeathWatch::destroyed == 1, "the caller owns it now");

    // clear() frees everything the graph still owns.
    DeathWatch::destroyed = 0;

    {
        Graph graph;

        for (int i = 0; i < 3; ++i)
        {
            auto plugin = std::make_unique<WatchedPlugin>();
            auto block = std::make_unique<BlockInstance>(std::move(plugin),
                                                         "n" + juce::String(i));
            block->prepare(kSampleRate, kBlockSize, false);
            graph.addBlockNode(std::move(block), i + 1, 0);
        }

        check(graph.getNumBlocks() == 3, "three blocks owned");
        graph.clear();
        check(DeathWatch::destroyed == 3, "clear() frees all three");
        check(graph.getNodes().size() == 2, "and leaves a bare IN and OUT");
    }
}

void testSpilloverKeepsTopology()
{
    std::printf("\nSpillover: a tail rings out through what it fed\n");

    // IN -> verb -> amp -> OUT, where "amp" inverts. Retire verb with a tail and
    // feed silence: verb's delay line still holds signal, which must come out
    // *through* the inverting amp. That sign is the whole test — the lane
    // renders retired blocks alone, so its tail would come out un-inverted.
    BlockPool pool;

    GraphEngine engine;
    auto& graph = engine.getGraph();

    auto verb = makeNode("verb", 1, 0);
    verb.block = pool.add("verb", 256, false);
    graph.addNode(verb);

    auto amp = makeNode("amp", 2, 0);
    amp.block = pool.add("amp", 0, true); // inverts
    graph.addNode(amp);

    wire(graph, kInputNodeUid, "verb");
    wire(graph, "verb", "amp");
    wire(graph, "amp", kOutputNodeUid);

    engine.prepare(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;

    // Prime verb's delay line with a known positive DC block.
    for (int i = 0; i < kBlockSize; ++i)
    {
        buffer.setSample(0, i, 0.5f);
        buffer.setSample(1, i, 0.5f);
    }

    engine.process(buffer, midi);

    // Retire verb with a one-second tail. The engine takes the block off the
    // graph's books itself.
    check(engine.retireWithTail("verb", 1.0), "verb retired with a tail");
    check(engine.getNumTailingBlocks() == 1, "one block is ringing out");

    // Silence in from here. Anything that comes out is tail.
    float mostNegative = 0.0f;
    float mostPositive = 0.0f;

    for (int block = 0; block < 4; ++block)
    {
        buffer.clear();
        engine.process(buffer, midi);

        for (int i = 0; i < kBlockSize; ++i)
        {
            mostNegative = juce::jmin(mostNegative, buffer.getSample(0, i));
            mostPositive = juce::jmax(mostPositive, buffer.getSample(0, i));
        }
    }

    std::printf("        tail range: %.4f .. %.4f\n", mostNegative, mostPositive);

    check(mostNegative < -0.4f, "the tail rings out");
    check(mostPositive < 0.01f,
          "and it is inverted, so it passed through the amp downstream of it");

    // Regression: cutting the tail's input must not orphan what is downstream
    // of it. Reachability-from-IN alone would mark amp and OUT dormant here, and
    // the rig would fall silent the moment a block started ringing out.
    const auto plan = engine.getGraph().compile(kBlockSize);
    const std::set<juce::String> dormant(plan.dormantUids.begin(), plan.dormantUids.end());

    check(dormant.count("amp") == 0, "the block downstream of a tail stays live");
    check(dormant.count(kOutputNodeUid) == 0, "OUT stays live while a tail rings");
    check(dormant.count("verb") == 0, "the tailing node itself is live, not dormant");
}

void testSpilloverExpires()
{
    std::printf("\nSpillover: the window closes and the block is reclaimed\n");

    BlockPool pool;

    GraphEngine engine;
    auto& graph = engine.getGraph();

    auto verb = makeNode("verb", 1, 0);
    verb.block = pool.add("verb", 64, false);
    graph.addNode(verb);

    wire(graph, kInputNodeUid, "verb");
    wire(graph, "verb", kOutputNodeUid);

    engine.prepare(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;

    // A short window: two blocks' worth.
    const double seconds = (2.0 * kBlockSize) / kSampleRate;
    engine.retireWithTail("verb", seconds);

    check(engine.getGraph().findNode("verb") != nullptr, "the node stays while it rings");

    for (int block = 0; block < 6; ++block)
    {
        buffer.clear();
        engine.process(buffer, midi);
        engine.collectGarbage();
    }

    check(engine.getNumTailingBlocks() == 0, "the tail expired");
    check(engine.getGraph().findNode("verb") == nullptr, "and its node left the graph");

    // The chain healed around it, so audio still reaches OUT.
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(0, i, 0.5f);

    engine.process(buffer, midi);
    check(buffer.getMagnitude(0, kBlockSize) > 0.4f, "IN reaches OUT after the tail is gone");
}

//==============================================================================
// Lane-shaped edits over a graph (transitional; G5 deletes this layer).

std::unique_ptr<BlockInstance> makeBlockFor(const juce::String& uid, int latency = 0,
                                            bool invert = false)
{
    auto plugin = std::make_unique<DelayPlugin>(latency, invert);
    auto block = std::make_unique<BlockInstance>(std::move(plugin), uid);
    block->prepare(kSampleRate, kBlockSize, false);
    return block;
}

bool wired(const Graph& graph, const juce::String& from, const juce::String& to)
{
    for (const auto& w : graph.getWires())
        if (w.fromUid == from && w.toUid == to)
            return true;

    return false;
}

void testLaneInsertSplices()
{
    std::printf("\nLane edits: insert splices into the path\n");

    Graph graph;

    BlockPosition append;
    append.stage = 0;
    append.row = 0;

    graphlane::insertBlock(graph, makeBlockFor("a"), append);

    check(wired(graph, kInputNodeUid, "a"), "first block: IN -> a");
    check(wired(graph, "a", kOutputNodeUid), "first block: a -> OUT");
    check(! wired(graph, kInputNodeUid, kOutputNodeUid),
          "and the wire that skipped past it is gone");

    // Append a second.
    append.stage = 1;
    graphlane::insertBlock(graph, makeBlockFor("b"), append);

    check(wired(graph, "a", "b") && wired(graph, "b", kOutputNodeUid), "IN -> a -> b -> OUT");
    check(! wired(graph, "a", kOutputNodeUid), "a no longer reaches OUT directly");

    // Insert in the middle: between a and b.
    BlockPosition middle;
    middle.stage = 1;
    middle.row = 0;
    middle.newStage = true;

    graphlane::insertBlock(graph, makeBlockFor("mid"), middle);

    check(wired(graph, "a", "mid") && wired(graph, "mid", "b"), "a -> mid -> b");
    check(! wired(graph, "a", "b"), "the old a -> b wire is removed, not left alongside");

    // The signal must pass through everything exactly once - a leftover skip
    // wire would sum a dry copy at the far end.
    const auto plan = graph.compile(kBlockSize);
    check(plan.steps.size() == 5, "IN, a, mid, b, OUT all scheduled");
    check(plan.dormantUids.empty(), "nothing orphaned");

    const auto* out = stepFor(plan, kOutputNodeUid);
    check(out != nullptr && out->inputs.size() == 1, "OUT takes exactly one input - no dry copy");

    check(graphlane::getNumStages(graph) == 3, "the lane sees three stages");
}

void testLaneRemoveHeals()
{
    std::printf("\nLane edits: remove heals the gap\n");

    Graph graph;

    for (int i = 0; i < 3; ++i)
    {
        BlockPosition position;
        position.stage = i;
        graphlane::insertBlock(graph, makeBlockFor("n" + juce::String(i)), position);
    }

    check(wired(graph, "n0", "n1") && wired(graph, "n1", "n2"), "chain built");

    check(graphlane::removeBlock(graph, "n1"), "middle block removed");
    check(wired(graph, "n0", "n2"), "and the chain healed across the gap");
    check(graph.findNode("n1") == nullptr, "the node is gone");

    check(! graphlane::removeBlock(graph, "nonexistent"), "an unknown uid is refused");
    check(! graphlane::removeBlock(graph, kInputNodeUid), "IN cannot be removed");

    const auto plan = graph.compile(kBlockSize);
    check(plan.dormantUids.empty(), "nothing is left dangling");
    check(plan.steps.size() == 4, "IN, n0, n2, OUT");
}

void testLaneMoveReorders()
{
    std::printf("\nLane edits: move reorders without stranding wires\n");

    Graph graph;

    for (int i = 0; i < 3; ++i)
    {
        BlockPosition position;
        position.stage = i;
        graphlane::insertBlock(graph, makeBlockFor("n" + juce::String(i)), position);
    }

    // Move the first block to the end.
    BlockPosition toEnd;
    toEnd.stage = 3;
    toEnd.row = 0;

    check(graphlane::moveBlock(graph, "n0", toEnd), "n0 moved to the end");

    check(wired(graph, kInputNodeUid, "n1"), "IN now feeds what followed it");
    check(wired(graph, "n2", "n0"), "and n0 sits after n2");
    check(wired(graph, "n0", kOutputNodeUid), "feeding OUT");
    check(! wired(graph, "n0", "n1"), "its old outgoing wire is gone");

    const auto plan = graph.compile(kBlockSize);
    check(plan.steps.size() == 5, "every block still renders");
    check(plan.dormantUids.empty(), "and none was stranded by the move");

    const auto* out = stepFor(plan, kOutputNodeUid);
    check(out != nullptr && out->inputs.size() == 1, "OUT still takes one input");

    // The block kept its identity and its plug-in across the move.
    check(graph.getBlockByUid("n0") != nullptr, "the moved block kept its block");
    check(graph.getNumBlocks() == 3, "no block was lost or duplicated");
}

void testLaneInsertOnSecondRow()
{
    std::printf("\nLane edits: a block on row 1 becomes a parallel branch\n");

    Graph graph;

    BlockPosition first;
    first.stage = 0;
    graphlane::insertBlock(graph, makeBlockFor("main"), first);

    BlockPosition below;
    below.stage = 0;
    below.row = 1;
    graphlane::insertBlock(graph, makeBlockFor("parallel"), below);

    check(wired(graph, kInputNodeUid, "parallel"), "IN feeds the second row");
    check(wired(graph, "parallel", kOutputNodeUid), "which rejoins at OUT");
    check(wired(graph, kInputNodeUid, "main"), "the first row is untouched");

    const auto plan = graph.compile(kBlockSize);
    const auto* out = stepFor(plan, kOutputNodeUid);

    check(out != nullptr && out->inputs.size() == 2, "OUT sums both branches");
    check(plan.dormantUids.empty(), "both branches are live");
    check(graphlane::isStageSplit(graph, 0), "and the lane reads the stage as split");
}

void testRenderAfterLaneEdits()
{
    std::printf("\nAudio still flows after a run of lane edits\n");

    GraphEngine engine;
    auto& graph = engine.getGraph();

    for (int i = 0; i < 4; ++i)
    {
        BlockPosition position;
        position.stage = i;
        graphlane::insertBlock(graph, makeBlockFor("n" + juce::String(i)), position);
    }

    engine.prepare(kSampleRate, kBlockSize);

    check(peakAfterPriming(engine, 6, 1) > 0.1f, "audio reaches OUT through four blocks");

    graphlane::removeBlock(graph, "n1");
    BlockPosition toFront;
    toFront.stage = 0;
    toFront.newStage = true;
    graphlane::moveBlock(graph, "n3", toFront);
    engine.publish();

    check(peakAfterPriming(engine, 6, 1) > 0.1f, "and still does after a remove and a move");
    check(engine.getGraph().getNumBlocks() == 3, "three blocks remain");
}

} // namespace

int main()
{
    // Rendering tests instantiate AudioProcessors, which JUCE requires a
    // message manager for.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    std::printf("Graph engine tests (G1)\n");

    testTopologyAndCycles();
    testFanOutAndFanIn();
    testLatencyAlignment();
    testDormantNodes();
    testBufferReuse();
    testHealAndRemove();
    testDiamond();
    testRenderPassThrough();
    testRenderFanInSums();
    testNullOnAlignedBranches();
    testPlanSwapWhileRendering();
    testGraphOwnsItsBlocks();
    testSpilloverKeepsTopology();
    testSpilloverExpires();
    testLaneInsertSplices();
    testLaneRemoveHeals();
    testLaneMoveReorders();
    testLaneInsertOnSecondRow();
    testRenderAfterLaneEdits();

    std::printf("\n%s (%d failure%s)\n",
                gFailures == 0 ? "ALL PASSED" : "FAILURES",
                gFailures,
                gFailures == 1 ? "" : "s");

    return gFailures == 0 ? 0 : 1;
}
