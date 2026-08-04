#include "host/Graph.h"

#include <algorithm>
#include <map>
#include <set>

namespace blockrig
{

Graph::Graph()
{
    GraphNode in;
    in.uid = kInputNodeUid;
    in.col = 0;
    in.row = 0;
    mNodes.push_back(std::move(in));

    GraphNode out;
    out.uid = kOutputNodeUid;
    out.col = 1;
    out.row = 0;
    mNodes.push_back(std::move(out));
}

bool Graph::hasNode(const juce::String& uid) const
{
    return findNode(uid) != nullptr;
}

GraphNode* Graph::findNode(const juce::String& uid)
{
    for (auto& node : mNodes)
        if (node.uid == uid)
            return &node;

    return nullptr;
}

const GraphNode* Graph::findNode(const juce::String& uid) const
{
    for (const auto& node : mNodes)
        if (node.uid == uid)
            return &node;

    return nullptr;
}

void Graph::addNode(GraphNode node)
{
    if (hasNode(node.uid))
        return;

    mNodes.push_back(std::move(node));
}

std::vector<juce::String> Graph::sourcesOf(const juce::String& uid) const
{
    std::vector<juce::String> result;

    for (const auto& wire : mWires)
        if (wire.toUid == uid)
            result.push_back(wire.fromUid);

    return result;
}

std::vector<juce::String> Graph::destinationsOf(const juce::String& uid) const
{
    std::vector<juce::String> result;

    for (const auto& wire : mWires)
        if (wire.fromUid == uid)
            result.push_back(wire.toUid);

    return result;
}

bool Graph::reaches(const juce::String& fromUid, const juce::String& targetUid) const
{
    if (fromUid == targetUid)
        return true;

    std::set<juce::String> visited;
    std::vector<juce::String> stack{fromUid};

    while (! stack.empty())
    {
        const auto current = stack.back();
        stack.pop_back();

        if (! visited.insert(current).second)
            continue;

        for (const auto& next : destinationsOf(current))
        {
            if (next == targetUid)
                return true;

            stack.push_back(next);
        }
    }

    return false;
}

std::vector<GraphWire> Graph::wiresAt(const juce::String& uid) const
{
    std::vector<GraphWire> result;

    for (const auto& wire : mWires)
        if (wire.fromUid == uid || wire.toUid == uid)
            result.push_back(wire);

    return result;
}

bool Graph::removeNode(const juce::String& uid)
{
    auto* node = findNode(uid);

    if (node == nullptr || node->isEndpoint())
        return false;

    mWires.erase(std::remove_if(mWires.begin(), mWires.end(),
                                [&uid](const GraphWire& wire)
                                { return wire.fromUid == uid || wire.toUid == uid; }),
                 mWires.end());

    mNodes.erase(std::remove_if(mNodes.begin(), mNodes.end(),
                                [&uid](const GraphNode& n) { return n.uid == uid; }),
                 mNodes.end());

    return true;
}

bool Graph::healAround(const juce::String& uid)
{
    const auto* node = findNode(uid);

    if (node == nullptr || node->isEndpoint())
        return false;

    const auto sources = sourcesOf(uid);
    const auto destinations = destinationsOf(uid);

    if (! removeNode(uid))
        return false;

    // Reconnect every source to every destination. For the common case — one in,
    // one out — this is the single healed wire the lane produces today. For a
    // node that was fanning out, each downstream branch keeps its feed.
    for (const auto& source : sources)
    {
        for (const auto& destination : destinations)
        {
            GraphWire healed;
            healed.fromUid = source;
            healed.toUid = destination;

            if (canAddWire(healed) == WireRejection::accepted)
                mWires.push_back(healed);
        }
    }

    return true;
}

WireRejection Graph::canAddWire(const GraphWire& wire) const
{
    if (! hasNode(wire.fromUid) || ! hasNode(wire.toUid))
        return WireRejection::unknownNode;

    if (wire.fromUid == wire.toUid)
        return WireRejection::selfConnection;

    // Nothing may feed IN, and OUT may not feed anything.
    if (wire.toUid == kInputNodeUid || wire.fromUid == kOutputNodeUid)
        return WireRejection::selfConnection;

    for (const auto& existing : mWires)
        if (existing == wire)
            return WireRejection::duplicate;

    // A cycle is exactly the case where the destination can already reach the
    // source. DAG-only is a v1 rule: feedback needs an explicit delay node.
    if (reaches(wire.toUid, wire.fromUid))
        return WireRejection::wouldCycle;

    return WireRejection::accepted;
}

bool Graph::addWire(const GraphWire& wire)
{
    if (canAddWire(wire) != WireRejection::accepted)
        return false;

    mWires.push_back(wire);
    return true;
}

bool Graph::removeWire(const GraphWire& wire)
{
    const auto before = mWires.size();

    mWires.erase(std::remove(mWires.begin(), mWires.end(), wire), mWires.end());

    return mWires.size() != before;
}

void Graph::refreshLatencies()
{
    for (auto& node : mNodes)
        node.latencySamples = node.block != nullptr ? node.block->getLatencySamples() : 0;
}

std::optional<std::vector<juce::String>> Graph::topologicalOrder() const
{
    std::map<juce::String, int> inDegree;

    for (const auto& node : mNodes)
        inDegree[node.uid] = 0;

    for (const auto& wire : mWires)
        ++inDegree[wire.toUid];

    // Seed with every source node. Kahn's algorithm; a leftover node at the end
    // means a cycle, which addWire should have made unreachable — this is the
    // backstop that keeps a corrupt loaded rig from producing a bad plan.
    std::vector<juce::String> ready;

    for (const auto& node : mNodes)
        if (inDegree[node.uid] == 0)
            ready.push_back(node.uid);

    std::vector<juce::String> order;
    order.reserve(mNodes.size());

    while (! ready.empty())
    {
        const auto current = ready.back();
        ready.pop_back();
        order.push_back(current);

        for (const auto& next : destinationsOf(current))
            if (--inDegree[next] == 0)
                ready.push_back(next);
    }

    if (order.size() != mNodes.size())
        return std::nullopt;

    return order;
}

RenderPlan Graph::compile(int maxBlockSize) const
{
    RenderPlan plan;

    const auto order = topologicalOrder();

    if (! order.has_value())
        return plan;

    // A node participates only if signal can reach it from IN and it can reach
    // OUT. Everything else is dormant: legal, rendered nowhere, dimmed on the
    // canvas. Parking a lead sound off to the side is a feature.
    std::set<juce::String> live;

    // Signal originates at IN and at every tailing node — a tail has had its
    // inputs cut, so IN cannot reach it, but it is still a source of audio.
    //
    // Testing reachability from IN alone is wrong in a way that is easy to miss:
    // cutting a tail's input also orphans everything *downstream* of it, so a
    // rig whose only path ran through the retired block would fall silent the
    // instant it started ringing out. The tail must light its own downstream.
    std::vector<juce::String> sources{kInputNodeUid};

    for (const auto& node : mNodes)
        if (node.isTailing)
            sources.push_back(node.uid);

    const auto reachableFromAnySource = [&](const juce::String& uid)
    {
        for (const auto& source : sources)
            if (reaches(source, uid))
                return true;

        return false;
    };

    for (const auto& uid : *order)
    {
        // The endpoints always run: OUT has to emit something even when nothing
        // feeds it, and IN has to publish the incoming audio even when nothing
        // consumes it.
        if (uid == kInputNodeUid || uid == kOutputNodeUid)
        {
            live.insert(uid);
            continue;
        }

        if (reachableFromAnySource(uid) && reaches(uid, kOutputNodeUid))
            live.insert(uid);
    }

    for (const auto& node : mNodes)
        if (live.count(node.uid) == 0)
            plan.dormantUids.push_back(node.uid);

    // Arrival time: when this node's input is ready, in samples relative to IN.
    // A node cannot run until its latest branch has arrived, so arrival is a max
    // over incoming branches; the difference per branch is the alignment delay.
    // This is the same maths the lane runs per stage (padSamples = stage.latency
    // - row.latency), generalized from rows to paths.
    std::map<juce::String, int> arrival;

    for (const auto& uid : *order)
    {
        if (live.count(uid) == 0)
            continue;

        int latest = 0;

        for (const auto& source : sourcesOf(uid))
        {
            if (live.count(source) == 0)
                continue;

            const auto* sourceNode = findNode(source);
            const int ready = arrival[source] + (sourceNode != nullptr ? sourceNode->latencySamples : 0);
            latest = juce::jmax(latest, ready);
        }

        arrival[uid] = latest;
    }

    plan.totalLatencySamples = arrival[kOutputNodeUid];

    // Buffer pool. Each live node writes one buffer; a buffer returns to the
    // free list once every consumer of that node has run.
    //
    // Deliberate simplification for G1: the output buffer is taken before the
    // node's inputs are released, so a step's output never aliases one of its
    // own inputs. That costs at most one extra pool buffer and removes a whole
    // class of ordering bug from the renderer, where a delayed branch reading
    // from a buffer the same step is writing would be silently wrong. Revisit
    // only if a real rig shows the pool getting expensive.
    std::map<juce::String, int> remainingConsumers;

    for (const auto& wire : mWires)
        if (live.count(wire.fromUid) > 0 && live.count(wire.toUid) > 0)
            ++remainingConsumers[wire.fromUid];

    std::map<juce::String, int> outputBuffer;
    std::vector<int> freeBuffers;
    int nextBuffer = 0;

    const auto acquireBuffer = [&]() -> int
    {
        if (! freeBuffers.empty())
        {
            const int index = freeBuffers.back();
            freeBuffers.pop_back();
            return index;
        }

        return nextBuffer++;
    };

    for (const auto& uid : *order)
    {
        if (live.count(uid) == 0)
            continue;

        const auto* node = findNode(uid);

        if (node == nullptr)
            continue;

        PlanStep step;
        step.uid = uid;
        step.block = node->block;
        step.tailSamplesLeft = node->tailSamplesLeft;

        for (const auto& wire : mWires)
        {
            if (wire.toUid != uid)
                continue;

            if (live.count(wire.fromUid) == 0)
                continue;

            const auto* sourceNode = findNode(wire.fromUid);
            const int ready = arrival[wire.fromUid]
                            + (sourceNode != nullptr ? sourceNode->latencySamples : 0);
            const int delaySamples = arrival[uid] - ready;

            PlanInput input;
            input.bufferIndex = outputBuffer[wire.fromUid];

            if (delaySamples > 0)
            {
                PlanDelay delay;
                delay.lengthSamples = delaySamples;
                // Allocated and cleared here on the message thread; the audio
                // thread only reads and writes within it.
                delay.buffer.setSize(2, delaySamples + maxBlockSize + 1, false, true, true);
                delay.buffer.clear();

                input.delayIndex = static_cast<int>(plan.delays.size());
                plan.delays.push_back(std::move(delay));
            }

            step.inputs.push_back(input);
        }

        step.outputBuffer = acquireBuffer();
        outputBuffer[uid] = step.outputBuffer;

        // Release upstream buffers only now that this step has its own, so an
        // output never aliases an input. See the note above.
        for (const auto& wire : mWires)
        {
            if (wire.toUid != uid || live.count(wire.fromUid) == 0)
                continue;

            if (--remainingConsumers[wire.fromUid] == 0)
                freeBuffers.push_back(outputBuffer[wire.fromUid]);
        }

        plan.steps.push_back(std::move(step));
    }

    plan.numBuffers = nextBuffer;

    // The pool itself, allocated and cleared here so process() only ever reads
    // and writes within it.
    plan.buffers.resize(static_cast<size_t>(nextBuffer));

    for (auto& buffer : plan.buffers)
    {
        buffer.setSize(2, maxBlockSize, false, true, true);
        buffer.clear();
    }

    return plan;
}

} // namespace blockrig
