#include "host/GraphEngine.h"

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

    publish();
}

void GraphEngine::release()
{
    mPrepared = false;
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

void GraphEngine::collectGarbage()
{
    if (auto* retired = mRetiredPlan.exchange(nullptr, std::memory_order_acq_rel))
        mGarbage.emplace_back(retired);

    mGarbage.clear();
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
    if (auto* incoming = mPendingPlan.exchange(nullptr, std::memory_order_acq_rel))
    {
        if (mActivePlan != nullptr)
        {
            if (auto* alreadyRetired = mRetiredPlan.exchange(mActivePlan, std::memory_order_acq_rel))
            {
                // The message thread has not drained yet and we cannot free on
                // this thread. Put it back and let the newer one wait a block.
                mRetiredPlan.store(alreadyRetired, std::memory_order_release);
            }
        }

        mActivePlan = incoming;
    }

    if (mActivePlan == nullptr || mActivePlan->steps.empty())
        return;

    const int numSamples = buffer.getNumSamples();
    const double bufferDuration = mSampleRate > 0.0 ? numSamples / mSampleRate : 0.0;

    auto& plan = *mActivePlan;

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

        if (step.block != nullptr)
            step.block->process(destination, midi, bufferDuration);

        if (step.uid == kOutputNodeUid)
        {
            const int channels = juce::jmin(destination.getNumChannels(), buffer.getNumChannels());

            for (int channel = 0; channel < channels; ++channel)
                buffer.copyFrom(channel, 0, destination, channel, 0, numSamples);
        }
    }
}

} // namespace blockrig
