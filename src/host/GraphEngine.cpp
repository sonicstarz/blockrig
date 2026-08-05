#include "host/GraphEngine.h"

#include <map>

#include "host/GraphLane.h"

namespace blockrig
{

GraphEngine::GraphEngine() = default;

GraphEngine::~GraphEngine()
{
    delete mPendingPlan.exchange(nullptr, std::memory_order_acq_rel);
    delete mRetiredPlan.exchange(nullptr, std::memory_order_acq_rel);
    delete mActivePlan;
}

void GraphEngine::prepare(double sampleRate, int maxBlockSize)
{
    mSampleRate = sampleRate;
    mMaxBlockSize = maxBlockSize;
    mPrepared = true;

    // Every block, unconditionally. Blocks can arrive before the device starts —
    // restoring a session does exactly that, adding the whole rig and only then
    // getting a sample rate — and prepareGraph() is a no-op while unprepared, so
    // those blocks are still sitting there uninitialised at this point. Handing
    // one to the audio thread means processBlock() writing through pointers its
    // plug-in never allocated.
    //
    // `true` rather than `false` because the sample rate or block size may have
    // changed since the last prepare, which invalidates every block's buffers
    // whatever their negotiated width says.
    prepareGraph(true);

    publish();
}

void GraphEngine::release()
{
    mPrepared = false;

    // Audio has stopped, so tails can never finish counting down: their windows
    // are decremented by the audio thread, and collectGarbage only frees a tail
    // once a plan retirement has been observed after it left the graph. Without
    // this they would sit held until the engine was destroyed — closing an audio
    // device mid-ring would strand every block that happened to be tailing.
    //
    // Safe to reclaim here: releaseResources is called after the host has
    // stopped the audio thread, so nothing is rendering these.
    for (auto& tail : mTails)
        if (auto* node = mGraph.findNode(tail->uid))
        {
            node->tailSamplesLeft = nullptr;
            mGraph.removeNode(tail->uid);
        }

    mTails.clear();
    mExpiredTails.clear();
}

void GraphEngine::clear()
{
    mGraph.clear();
    mTails.clear();
    mExpiredTails.clear();
    publish();
}

int GraphEngine::getNumStages() const { return graphlane::getNumStages(mGraph); }

int GraphEngine::getNumRows(int stage) const { return graphlane::getNumRows(mGraph, stage); }

bool GraphEngine::isStageSplit(int stage) const { return graphlane::isStageSplit(mGraph, stage); }

std::vector<BlockInstance*> GraphEngine::getBlocksInRow(int stage, int row) const
{
    return graphlane::getBlocksInRow(mGraph, stage, row);
}

BlockInstance* GraphEngine::getBlockByIndex(int index) const
{
    return graphlane::getBlockByIndex(mGraph, index);
}

std::optional<BlockPosition> GraphEngine::findBlock(const juce::String& uid) const
{
    return graphlane::findBlock(mGraph, uid);
}

void GraphEngine::insertBlock(std::unique_ptr<BlockInstance> block, BlockPosition position)
{
    graphlane::insertBlock(mGraph, std::move(block), position);
    prepareGraph(false);
    publish();
}

void GraphEngine::removeBlock(const juce::String& uid)
{
    // Removal rings out. The lane did this inside its own removeBlock, so
    // deleting the block outright here would have quietly dropped spillover —
    // the one feature P11 exists for — while every test still passed, because
    // nothing asserts that removing a reverb leaves a tail behind.
    const auto* node = mGraph.findNode(uid);
    const bool canTail = node != nullptr && node->block != nullptr && mTailCarrySeconds > 0.0;

    if (canTail)
    {
        if (retireWithTail(uid, mTailCarrySeconds))
        {
            prepareGraph(false);
            publish();
        }

        return;
    }

    if (graphlane::removeBlock(mGraph, uid))
    {
        prepareGraph(false);
        publish();
    }
}

void GraphEngine::moveBlock(const juce::String& uid, BlockPosition position)
{
    if (graphlane::moveBlock(mGraph, uid, position))
    {
        // A move can change what feeds a block, so widths may need renegotiating
        // even though no block was added or removed.
        prepareGraph(false);
        publish();
    }
}

namespace
{
/// Finds a plug-in parameter by its APVTS id. Built-in blocks declare stable
/// ids ("gain", "pan"), which is what lets the lane's row controls reach the
/// Utility block that now carries those values.
juce::RangedAudioParameter* parameterWithId(BlockInstance* block, const juce::String& id)
{
    if (block == nullptr)
        return nullptr;

    auto* plugin = block->getPlugin();

    if (plugin == nullptr)
        return nullptr;

    for (auto* parameter : plugin->getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            if (withId->paramID == id)
                return withId;

    return nullptr;
}

/// The block a lane row's gain/pan controls should act on: the first block in
/// that cell that actually exposes them. For a migrated dualMono split that is
/// the Utility node the migration minted at the head of the branch.
BlockInstance* rowControlBlock(const std::vector<BlockInstance*>& blocks, const juce::String& id)
{
    for (auto* block : blocks)
        if (parameterWithId(block, id) != nullptr)
            return block;

    return nullptr;
}

void setParameter(juce::RangedAudioParameter* parameter, float value)
{
    if (parameter != nullptr)
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

float getParameter(juce::RangedAudioParameter* parameter, float fallback)
{
    return parameter != nullptr ? parameter->convertFrom0to1(parameter->getValue()) : fallback;
}
} // namespace

void GraphEngine::setRowGainDb(int stage, int row, float gainDb)
{
    setParameter(parameterWithId(rowControlBlock(getBlocksInRow(stage, row), "gain"), "gain"),
                 gainDb);
}

void GraphEngine::setRowPan(int stage, int row, float pan)
{
    setParameter(parameterWithId(rowControlBlock(getBlocksInRow(stage, row), "pan"), "pan"), pan);
}

float GraphEngine::getRowGainDb(int stage, int row) const
{
    auto blocks = const_cast<GraphEngine*>(this)->getBlocksInRow(stage, row);
    return getParameter(parameterWithId(rowControlBlock(blocks, "gain"), "gain"), 0.0f);
}

float GraphEngine::getRowPan(int stage, int row) const
{
    auto blocks = const_cast<GraphEngine*>(this)->getBlocksInRow(stage, row);
    return getParameter(parameterWithId(rowControlBlock(blocks, "pan"), "pan"), 0.0f);
}

bool GraphEngine::rowHasControls(int stage, int row) const
{
    auto blocks = const_cast<GraphEngine*>(this)->getBlocksInRow(stage, row);
    return rowControlBlock(blocks, "gain") != nullptr;
}

void GraphEngine::setSourceIsMono(bool sourceIsMono)
{
    if (mSourceIsMono == sourceIsMono)
        return;

    mSourceIsMono = sourceIsMono;
    prepareGraph(false);
    publish();
}

void GraphEngine::setPlayHead(juce::AudioPlayHead* playHead)
{
    mPlayHead = playHead;

    for (auto* block : mGraph.getBlocks())
        if (auto* plugin = block->getPlugin())
            plugin->setPlayHead(playHead);
}

void GraphEngine::walkWidths(bool force,
                             const std::function<void(BlockInstance&, bool)>& visit) const
{
    const auto order = mGraph.topologicalOrder();

    if (! order.has_value())
        return;

    // Whether each node's *output* is still a single channel.
    std::map<juce::String, bool> outputIsMono;
    outputIsMono[kInputNodeUid] = mSourceIsMono;

    for (const auto& uid : *order)
    {
        const auto* node = mGraph.findNode(uid);

        if (node == nullptr || node->isEndpoint())
            continue;

        // A node is mono-fed only when every branch reaching it is mono. One
        // stereo branch makes the merge stereo — the generalization of the
        // lane's per-stage rule to arbitrary fan-in.
        bool monoIn = mSourceIsMono;
        bool sawSource = false;

        for (const auto& wire : mGraph.getWires())
        {
            if (wire.toUid != uid)
                continue;

            const auto it = outputIsMono.find(wire.fromUid);
            const bool branchIsMono = it != outputIsMono.end() ? it->second : mSourceIsMono;

            monoIn = sawSource ? (monoIn && branchIsMono) : branchIsMono;
            sawSource = true;
        }

        if (node->block != nullptr)
        {
            auto& block = *node->block;

            if (force || ! block.hasBeenPrepared() || block.layoutDrifted()
                || block.getSourceIsMono() != monoIn)
                visit(block, monoIn);

            // Once something emits two channels the signal is no longer a single
            // channel, whatever it does with them.
            outputIsMono[uid] = monoIn && ! block.producesStereo();
        }
        else
        {
            outputIsMono[uid] = monoIn;
        }
    }
}

void GraphEngine::prepareGraph(bool force)
{
    if (! mPrepared)
        return;

    // Two passes, as the lane does it: the first only decides whether anything
    // needs re-preparing, so the audio thread is suspended exactly when a live
    // plug-in is about to be touched and not on every edit.
    bool anyToPrepare = false;
    walkWidths(force, [&anyToPrepare](BlockInstance&, bool) { anyToPrepare = true; });

    if (! anyToPrepare)
        return;

    if (suspendAudio)
        suspendAudio(true);

    walkWidths(force,
               [this](BlockInstance& block, bool monoIn)
               { block.prepare(mSampleRate, mMaxBlockSize, monoIn); });

    if (suspendAudio)
        suspendAudio(false);
}

bool GraphEngine::refreshLatency()
{
    if (! mGraph.refreshLatencies())
        return false;

    publish();
    return true;
}

void GraphEngine::publish()
{
    if (! mPrepared)
        return;

    mGraph.refreshLatencies();

    auto plan = std::make_unique<RenderPlan>(mGraph.compile(mMaxBlockSize));

    mPublishedLatency.store(plan->totalLatencySamples, std::memory_order_release);

    // Whatever was pending and never adopted is ours to delete: the audio thread
    // has not seen it.
    RenderPlan* previous = mPendingPlan.exchange(plan.release(), std::memory_order_acq_rel);
    delete previous;
}

bool GraphEngine::retireWithTail(const juce::String& uid, double seconds)
{
    auto* node = mGraph.findNode(uid);

    if (node == nullptr || node->isEndpoint())
        return false;

    if (seconds <= 0.0 || ! mPrepared || node->block == nullptr)
    {
        mGraph.healAround(uid);
        publish();
        return true;
    }

    // The block has to outlive its node, so take it off the graph's books before
    // the node's window starts closing. A null here means the block was never
    // graph-owned — a caller keeping its own storage — which still tails
    // correctly: the node keeps rendering and the caller keeps the lifetime.
    auto block = mGraph.releaseBlock(uid);

    // Cut the inputs but keep the outputs: from here the node renders silence
    // through whatever it used to feed.
    //
    // The live path has to heal in the same breath. Removing a block mid-chain
    // and only cutting its input would leave everything downstream fed by the
    // dying tail alone — the rig would duck for the length of the tail and then
    // fall silent. So the sources reconnect straight to the destinations, and
    // for the tail's duration the destination sums both: the live signal, and
    // the retired block ringing out into it.
    std::vector<juce::String> sources;
    std::vector<juce::String> destinations;

    for (const auto& wire : mGraph.wiresAt(uid))
    {
        if (wire.toUid == uid)
            sources.push_back(wire.fromUid);
        else
            destinations.push_back(wire.toUid);
    }

    for (const auto& wire : mGraph.wiresAt(uid))
        if (wire.toUid == uid)
            mGraph.removeWire(wire);

    for (const auto& source : sources)
    {
        for (const auto& destination : destinations)
        {
            GraphWire healed;
            healed.fromUid = source;
            healed.toUid = destination;
            mGraph.addWire(healed); // refuses duplicates and cycles on its own
        }
    }

    auto tail = std::make_unique<Tail>();
    tail->block = std::move(block);
    tail->uid = uid;
    tail->samplesLeft.store(static_cast<int>(seconds * mSampleRate), std::memory_order_release);

    node->isTailing = true;
    node->tailSamplesLeft = &tail->samplesLeft;

    mTails.push_back(std::move(tail));

    publish();
    return true;
}

void GraphEngine::collectGarbage()
{
    // 1. Reclaim any plan the audio thread has finished with. Deleting here on
    //    the message thread is the whole point of the retirement slot.
    if (auto* retired = mRetiredPlan.exchange(nullptr, std::memory_order_acq_rel))
    {
        delete retired;
        ++mRetirementsSeen;
    }

    // 2. Free expired tails whose plan is provably gone. A tail's block cannot
    //    be freed the moment its window closes: the plan the audio thread is
    //    *still running* contains a step pointing at that block, and at the
    //    atomic counter living beside it. Only once a retirement has been seen
    //    after the tail left the graph has the audio thread adopted a plan
    //    without it. Getting this wrong is a use-after-free on the audio thread
    //    that would only show up under load.
    for (auto it = mExpiredTails.begin(); it != mExpiredTails.end();)
    {
        if (mRetirementsSeen > (*it)->retirementMark)
            it = mExpiredTails.erase(it);
        else
            ++it;
    }

    // 3. Tails whose window has closed leave the graph and start that wait.
    bool removedAny = false;

    for (auto it = mTails.begin(); it != mTails.end();)
    {
        if ((*it)->samplesLeft.load(std::memory_order_acquire) > 0)
        {
            ++it;
            continue;
        }

        if (auto* node = mGraph.findNode((*it)->uid))
        {
            // A plain removal: the live path was already healed when the tail
            // started, so there is nothing left to reconnect.
            node->tailSamplesLeft = nullptr;
            mGraph.removeNode((*it)->uid);
        }

        (*it)->retirementMark = mRetirementsSeen;
        mExpiredTails.push_back(std::move(*it));
        it = mTails.erase(it);
        removedAny = true;
    }

    if (removedAny)
        publish();
}

void GraphEngine::addBranch(juce::AudioBuffer<float>& destination,
                            const juce::AudioBuffer<float>& source,
                            PlanDelay* delay,
                            int numSamples) noexcept
{
    const int numChannels = juce::jmin(destination.getNumChannels(), source.getNumChannels());

    if (delay == nullptr || delay->lengthSamples <= 0 || delay->buffer.getNumSamples() == 0)
    {
        for (int channel = 0; channel < numChannels; ++channel)
            destination.addFrom(channel, 0, source, channel, 0, numSamples);

        return;
    }

    // Circular delay line, the same shape as the lane's row padding: write this
    // block's samples in, read the ones from `lengthSamples` ago out, and sum
    // those into the destination.
    const int capacity = delay->buffer.getNumSamples();

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto* input = source.getReadPointer(channel);
        auto* output = destination.getWritePointer(channel);
        auto* line = delay->buffer.getWritePointer(channel);

        int writePosition = delay->writePosition;

        for (int i = 0; i < numSamples; ++i)
        {
            const int readPosition = (writePosition + capacity - delay->lengthSamples) % capacity;
            const float delayed = line[readPosition];
            line[writePosition] = input[i];
            output[i] += delayed;
            writePosition = (writePosition + 1) % capacity;
        }
    }

    delay->writePosition = (delay->writePosition + numSamples) % capacity;
}

void GraphEngine::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept
{
    // Adopt a new plan if one is waiting. The plan we were running goes to the
    // retirement slot for the message thread to free.
    // Adopt a new plan only when there is somewhere to put the old one. Taking
    // the pending plan while the retirement slot is still full would strand the
    // outgoing plan: nothing would point at it, and it could never be freed —
    // a leak of a whole RenderPlan, buffer pool and delay lines included, on
    // every swap during a drag.
    //
    // Leaving the plan pending is safe and self-correcting. publish() replaces
    // a superseded pending plan on the message thread, so waiting a block costs
    // at most one block of staleness and never a lost edit.
    if (mRetiredPlan.load(std::memory_order_acquire) == nullptr)
    {
        if (auto* incoming = mPendingPlan.exchange(nullptr, std::memory_order_acq_rel))
        {
            if (mActivePlan != nullptr)
                mRetiredPlan.store(mActivePlan, std::memory_order_release);

            mActivePlan = incoming;
        }
    }

    if (mActivePlan == nullptr || mActivePlan->steps.empty())
        return;

    const int numSamples = buffer.getNumSamples();

    auto& plan = *mActivePlan;

    // The pool was sized for maxBlockSize at compile time and cannot grow here.
    // A host that hands us a larger block than it promised — a device change
    // that skips prepareToPlay, a plug-in host with a looser contract — would
    // otherwise write straight past the end of every pool buffer and delay line.
    //
    // Silence for one block is a bad outcome; heap corruption on the audio
    // thread is a far worse one, and it would present as a crash nowhere near
    // the cause. Count it as a dropout so it shows in the meter rather than
    // passing unnoticed.
    if (! plan.buffers.empty() && numSamples > plan.buffers.front().getNumSamples())
    {
        buffer.clear();
        mDropouts.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const double bufferDuration = mSampleRate > 0.0 ? numSamples / mSampleRate : 0.0;

    const auto start = juce::Time::getHighResolutionTicks();

    for (auto& step : plan.steps)
    {
        auto& destination = plan.buffers[static_cast<size_t>(step.outputBuffer)];

        if (step.uid == kInputNodeUid)
        {
            // IN publishes the rig's incoming audio into the pool.
            const int channels = juce::jmin(destination.getNumChannels(), buffer.getNumChannels());

            for (int channel = 0; channel < channels; ++channel)
                destination.copyFrom(channel, 0, buffer, channel, 0, numSamples);

            continue;
        }

        // Sum every incoming branch, each through its own alignment delay.
        destination.clear(0, numSamples);

        for (const auto& input : step.inputs)
        {
            PlanDelay* delay = input.delayIndex >= 0
                                 ? &plan.delays[static_cast<size_t>(input.delayIndex)]
                                 : nullptr;

            addBranch(destination,
                      plan.buffers[static_cast<size_t>(input.bufferIndex)],
                      delay,
                      numSamples);
        }

        if (step.tailSamplesLeft != nullptr)
        {
            // A ringing-out block. Once its window closes we stop calling it and
            // leave the buffer silent, so it costs nothing while the message
            // thread gets round to reclaiming it.
            const int remaining = step.tailSamplesLeft->load(std::memory_order_acquire);

            if (remaining <= 0)
                continue;

            step.tailSamplesLeft->store(juce::jmax(0, remaining - numSamples),
                                        std::memory_order_release);
        }

        if (step.block != nullptr)
            step.block->process(destination, midi, bufferDuration);

        if (step.uid == kOutputNodeUid)
        {
            const int channels = juce::jmin(destination.getNumChannels(), buffer.getNumChannels());

            for (int channel = 0; channel < channels; ++channel)
                buffer.copyFrom(channel, 0, destination, channel, 0, numSamples);
        }
    }

    // Whole-graph cost as a fraction of the buffer's budget, tails included —
    // spillover doubles the work while it rings, and the meter has to say so
    // rather than flatter the rig.
    const auto elapsed =
        juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);

    if (bufferDuration > 0.0)
    {
        const auto fraction = static_cast<float>(elapsed / bufferDuration);
        mTotalLoad.addMeasurement(fraction);

        if (fraction >= 1.0f)
            mDropouts.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace blockrig
