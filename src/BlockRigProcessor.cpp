#include "BlockRigProcessor.h"

#include "state/RigState.h"

namespace blockrig
{
namespace
{
constexpr float kSmoothingSeconds = 0.02f;
constexpr double kFallbackSampleRate = 48000.0;
constexpr int kFallbackBlockSize = 512;
} // namespace

/// Plug-ins may change their reported latency at any time — switching lookahead
/// on, changing oversampling — without telling the host. Polling at a low rate
/// is cheaper and more reliable than trying to observe every plug-in.
class BlockRigProcessor::LatencyPoller final : private juce::Timer
{
public:
    explicit LatencyPoller(BlockRigProcessor& owner)
        : mOwner(owner)
    {
        startTimer(1000);
    }

    ~LatencyPoller() override { stopTimer(); }

private:
    void timerCallback() override
    {
        if (mOwner.mChain.refreshLatency())
            mOwner.updateLatency();

        mOwner.mChain.collectGarbage();
    }

    BlockRigProcessor& mOwner;
};

BlockRigProcessor::BlockRigProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    mCatalog.loadFromStorage();
    mChain.setPlayHead(&mTransport);
    mLatencyPoller = std::make_unique<LatencyPoller>(*this);

    // SmoothedValue starts at 0, so if a processBlock ever arrives before
    // prepareToPlay every gain multiplies the signal by zero and the app is
    // silent with no other symptom. Give them real values up front.
    mInputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(mInputGainDb));
    mOutputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(mOutputGainDb));
    mMuteGain.setCurrentAndTargetValue(isMuted() ? 0.0f : 1.0f);
}

BlockRigProcessor::~BlockRigProcessor()
{
    mLatencyPoller.reset();
    mChain.clear();
    mChain.collectGarbage();
}

void BlockRigProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    mTransport.prepare(sampleRate);
    mChain.prepare(sampleRate, samplesPerBlock);
    mChain.setPlayHead(&mTransport);

    mInputGain.reset(sampleRate, static_cast<double>(kSmoothingSeconds));
    mOutputGain.reset(sampleRate, static_cast<double>(kSmoothingSeconds));
    mInputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(mInputGainDb));
    mOutputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(mOutputGainDb));

    // Ramp rather than jump, so un-muting does not click.
    mMuteGain.reset(sampleRate, 0.03);
    mMuteGain.setCurrentAndTargetValue(isMuted() ? 0.0f : 1.0f);

    updateLatency();
}

void BlockRigProcessor::releaseResources()
{
    mChain.release();
}

bool BlockRigProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void BlockRigProcessor::setInputGainDb(float gainDb)
{
    mInputGainDb = gainDb;
    mInputGain.setTargetValue(juce::Decibels::decibelsToGain(gainDb));
}

void BlockRigProcessor::setOutputGainDb(float gainDb)
{
    mOutputGainDb = gainDb;
    mOutputGain.setTargetValue(juce::Decibels::decibelsToGain(gainDb));
}

void BlockRigProcessor::setMuted(bool shouldBeMuted)
{
    mMuted.store(shouldBeMuted, std::memory_order_relaxed);
    mMuteGain.setTargetValue(shouldBeMuted ? 0.0f : 1.0f);
}

void BlockRigProcessor::startTestTone()
{
    const auto sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    mTestToneSamplesLeft.store(static_cast<int>(sampleRate * 2.0), std::memory_order_relaxed);
}

void BlockRigProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numInputs = getTotalNumInputChannels();
    const int numOutputs = getTotalNumOutputChannels();

    mProcessBlockCount.fetch_add(1, std::memory_order_relaxed);

    if (numSamples <= 0)
        return;

    // Inside a DAW the host owns the tempo, so mirror it and let our own clock
    // idle; standalone there is no other source, so we run the clock ourselves.
    if (auto* hostPlayHead = getPlayHead())
    {
        if (const auto hostPosition = hostPlayHead->getPosition())
            mTransport.followHost(*hostPosition);
        else
            mTransport.setFollowingHost(false);
    }
    else
    {
        mTransport.setFollowingHost(false);
    }

    mTransport.advance(numSamples);

    // The input end of the lane. Mono is the guitarist's case: one channel feeds
    // the whole (stereo) lane, so effects downstream can widen it.
    if (mInputMode == InputMode::mono && numOutputs >= 2)
    {
        const auto* source = numInputs > 0 ? buffer.getReadPointer(0) : nullptr;

        if (source != nullptr)
        {
            if (buffer.getNumChannels() >= 2)
                juce::FloatVectorOperations::copy(buffer.getWritePointer(1), source, numSamples);
        }
        else
        {
            buffer.clear();
        }
    }
    else if (numInputs == 1 && numOutputs >= 2 && buffer.getNumChannels() >= 2)
    {
        juce::FloatVectorOperations::copy(buffer.getWritePointer(1), buffer.getReadPointer(0), numSamples);
    }

    for (int channel = 0; channel < juce::jmin(numOutputs, buffer.getNumChannels()); ++channel)
    {
        auto* data = buffer.getWritePointer(channel);
        auto gain = mInputGain;
        for (int i = 0; i < numSamples; ++i)
            data[i] *= gain.getNextValue();
    }
    mInputGain.skip(numSamples);

    mInputLevel.store(buffer.getMagnitude(0, numSamples), std::memory_order_relaxed);

    mChain.process(buffer, midi);

    for (int channel = 0; channel < juce::jmin(numOutputs, buffer.getNumChannels()); ++channel)
    {
        auto* data = buffer.getWritePointer(channel);
        auto gain = mOutputGain;
        for (int i = 0; i < numSamples; ++i)
            data[i] *= gain.getNextValue();
    }
    mOutputGain.skip(numSamples);

    // Mute last, so it silences the rig no matter what any block is doing.
    for (int channel = 0; channel < juce::jmin(numOutputs, buffer.getNumChannels()); ++channel)
    {
        auto* data = buffer.getWritePointer(channel);
        auto gain = mMuteGain;
        for (int i = 0; i < numSamples; ++i)
            data[i] *= gain.getNextValue();
    }
    mMuteGain.skip(numSamples);

    // The test tone deliberately sits after the mute: its whole purpose is to
    // prove the path from this app to the speakers, so nothing may swallow it.
    if (auto remaining = mTestToneSamplesLeft.load(std::memory_order_relaxed); remaining > 0)
    {
        const auto sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        const auto increment = 2.0 * juce::MathConstants<double>::pi * 440.0 / sampleRate;
        const int toPlay = juce::jmin(numSamples, remaining);

        for (int i = 0; i < toPlay; ++i)
        {
            const auto value = static_cast<float>(0.12 * std::sin(mTestTonePhase));
            mTestTonePhase += increment;

            for (int channel = 0; channel < juce::jmin(numOutputs, buffer.getNumChannels()); ++channel)
                buffer.getWritePointer(channel)[i] = value;
        }

        mTestToneSamplesLeft.store(remaining - toPlay, std::memory_order_relaxed);
    }

    mOutputLevel.store(buffer.getMagnitude(0, numSamples), std::memory_order_relaxed);
}

void BlockRigProcessor::updateLatency()
{
    const int latency = mChain.getLatencySamples();

    if (latency != mReportedLatency)
    {
        mReportedLatency = latency;
        setLatencySamples(latency);
    }
}

void BlockRigProcessor::addBlock(const juce::PluginDescription& description, BlockPosition position,
                                 std::function<void(juce::String, juce::String)> onFinished)
{
    const double sampleRate = getSampleRate() > 0.0 ? getSampleRate() : kFallbackSampleRate;
    const int blockSize = getBlockSize() > 0 ? getBlockSize() : kFallbackBlockSize;

    // Async because AUv3 cannot be instantiated synchronously, and because a slow
    // plug-in must not freeze the UI while it loads.
    mCatalog.getFormatManager().createPluginInstanceAsync(
        description, sampleRate, blockSize,
        [this, position, onFinished](std::unique_ptr<juce::AudioPluginInstance> instance,
                                     const juce::String& error) {
            if (instance == nullptr)
            {
                if (onFinished)
                    onFinished({}, error.isNotEmpty() ? error : juce::String("Could not create plug-in"));
                return;
            }

            const auto uid = juce::Uuid().toDashedString().substring(0, 8);
            mChain.insertBlock(std::make_unique<BlockInstance>(std::move(instance), uid), position);
            updateLatency();

            if (onChainChanged)
                onChainChanged();

            if (onFinished)
                onFinished(uid, {});
        });
}

void BlockRigProcessor::removeBlock(const juce::String& uid)
{
    mChain.removeBlock(uid);
    updateLatency();

    if (onChainChanged)
        onChainChanged();
}

void BlockRigProcessor::moveBlock(const juce::String& uid, BlockPosition position)
{
    mChain.moveBlock(uid, position);

    if (onChainChanged)
        onChainChanged();
}

void BlockRigProcessor::splitStage(int stageIndex)
{
    if (mChain.splitStage(stageIndex) && onChainChanged)
        onChainChanged();
}

void BlockRigProcessor::mergeStage(int stageIndex)
{
    if (mChain.mergeStage(stageIndex))
    {
        updateLatency();
        if (onChainChanged)
            onChainChanged();
    }
}

void BlockRigProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const auto rig = rigstate::toValueTree(*this);
    juce::MemoryOutputStream stream(destData, false);
    rig.writeToStream(stream);
}

void BlockRigProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
    const auto rig = juce::ValueTree::readFromStream(stream);
    rigstate::restore(*this, rig);
}

} // namespace blockrig
