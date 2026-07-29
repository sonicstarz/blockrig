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
    mLatencyPoller = std::make_unique<LatencyPoller>(*this);
}

BlockRigProcessor::~BlockRigProcessor()
{
    mLatencyPoller.reset();
    mChain.clear();
    mChain.collectGarbage();
}

void BlockRigProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    mChain.prepare(sampleRate, samplesPerBlock);

    mInputGain.reset(sampleRate, static_cast<double>(kSmoothingSeconds));
    mOutputGain.reset(sampleRate, static_cast<double>(kSmoothingSeconds));
    mInputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(mInputGainDb));
    mOutputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(mOutputGainDb));

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

void BlockRigProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numInputs = getTotalNumInputChannels();
    const int numOutputs = getTotalNumOutputChannels();

    if (numSamples <= 0)
        return;

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

void BlockRigProcessor::addBlock(const juce::PluginDescription& description, int index,
                                 std::function<void(juce::String, juce::String)> onFinished)
{
    const double sampleRate = getSampleRate() > 0.0 ? getSampleRate() : kFallbackSampleRate;
    const int blockSize = getBlockSize() > 0 ? getBlockSize() : kFallbackBlockSize;

    // Async because AUv3 cannot be instantiated synchronously, and because a slow
    // plug-in must not freeze the UI while it loads.
    mCatalog.getFormatManager().createPluginInstanceAsync(
        description, sampleRate, blockSize,
        [this, index, onFinished](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) {
            if (instance == nullptr)
            {
                if (onFinished)
                    onFinished({}, error.isNotEmpty() ? error : juce::String("Could not create plug-in"));
                return;
            }

            const auto uid = juce::Uuid().toDashedString().substring(0, 8);
            mChain.insertBlock(std::make_unique<BlockInstance>(std::move(instance), uid), index);
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

void BlockRigProcessor::moveBlock(const juce::String& uid, int newIndex)
{
    mChain.moveBlock(uid, newIndex);

    if (onChainChanged)
        onChainChanged();
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
