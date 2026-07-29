#include "host/BlockInstance.h"

namespace blockrig
{

BlockInstance::BlockInstance(std::unique_ptr<juce::AudioPluginInstance> plugin, juce::String blockUid)
    : mPlugin(std::move(plugin))
    , mUid(std::move(blockUid))
{
    if (mPlugin != nullptr)
        mBypassParameter = mPlugin->getBypassParameter();
}

BlockInstance::~BlockInstance()
{
    if (mPlugin != nullptr)
        mPlugin->releaseResources();
}

void BlockInstance::prepare(double sampleRate, int maxBlockSize)
{
    if (mPlugin == nullptr)
        return;

    // Everything in the lane is stereo; JUCE negotiates what the plug-in can
    // actually do, and mono plug-ins get handled by the chain's channel setup.
    mPlugin->setPlayConfigDetails(mPlugin->getTotalNumInputChannels() > 0 ? 2 : 0, 2, sampleRate, maxBlockSize);
    mPlugin->prepareToPlay(sampleRate, maxBlockSize);
    mLoad.reset();
}

void BlockInstance::release()
{
    if (mPlugin != nullptr)
        mPlugin->releaseResources();
}

juce::String BlockInstance::getDisplayName() const
{
    return mPlugin != nullptr ? mPlugin->getName() : juce::String("(empty)");
}

void BlockInstance::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                            double bufferDurationSeconds) noexcept
{
    if (mPlugin == nullptr)
        return;

    const bool bypassed = isBypassed();

    // Drive the plug-in's own bypass parameter when it has one: plug-ins often
    // crossfade or flush tails properly, which processBlockBypassed cannot do.
    if (mBypassParameter != nullptr && bypassed != mLastAppliedBypass)
    {
        mBypassParameter->setValue(bypassed ? 1.0f : 0.0f);
        mLastAppliedBypass = bypassed;
    }

    const auto start = juce::Time::getHighResolutionTicks();

    if (bypassed && mBypassParameter == nullptr)
        mPlugin->processBlockBypassed(buffer, midi);
    else
        mPlugin->processBlock(buffer, midi);

    const auto elapsedSeconds =
        juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);

    if (bufferDurationSeconds > 0.0)
        mLoad.addMeasurement(static_cast<float>(elapsedSeconds / bufferDurationSeconds));

    // Peak level leaving this block, with a slow fall so the tile's meter is
    // readable at 15 Hz rather than flickering.
    float peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = juce::jmax(peak, buffer.getMagnitude(channel, 0, buffer.getNumSamples()));

    const auto previous = mOutputLevel.load(std::memory_order_relaxed);
    mOutputLevel.store(peak > previous ? peak : previous * 0.82f, std::memory_order_relaxed);
}

} // namespace blockrig
