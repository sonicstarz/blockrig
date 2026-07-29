#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "state/PluginState.h"

namespace nammodeler
{
namespace
{
constexpr float kSmoothingSeconds = 0.02f;

float parameterValue(const juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
{
    if (auto* raw = apvts.getRawParameterValue(id))
        return raw->load(std::memory_order_relaxed);
    jassertfalse;
    return 0.0f;
}

bool boolParameter(const juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
{
    return parameterValue(apvts, id) >= 0.5f;
}

/// Constant-power (sin/cos, -3 dB centre) pan law. Keeps perceived level stable
/// across the image and sums predictably to mono.
void constantPowerPan(float pan, float& leftGain, float& rightGain)
{
    const float normalised = juce::jlimit(0.0f, 1.0f, (pan + 1.0f) * 0.5f);
    const float angle = normalised * juce::MathConstants<float>::halfPi;
    leftGain = std::cos(angle);
    rightGain = std::sin(angle);
}
} // namespace

NAMModelerProcessor::NAMModelerProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , mApvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (int i = 0; i < kNumSlots; ++i)
        mLoader.attachSlot(i, &mSlots[static_cast<size_t>(i)]);

    mLoader.onLoadFinished = [this](int slotIndex, ModelInfo info, juce::String error) {
        if (!juce::isPositiveAndBelow(slotIndex, kNumSlots))
            return;

        mModelInfo[static_cast<size_t>(slotIndex)] = std::move(info);
        mSlotErrors[static_cast<size_t>(slotIndex)] = std::move(error);
        updateLatency();

        if (onModelStateChanged)
            onModelStateChanged();
    };
}

NAMModelerProcessor::~NAMModelerProcessor()
{
    // Tear the loader down first: it is the only thread allowed to destroy
    // models, and it holds pointers into the slots.
    mLoader.onLoadFinished = nullptr;
}

void NAMModelerProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    mLoader.setAudioConfiguration(sampleRate, samplesPerBlock);

    for (auto& slot : mSlots)
        slot.prepare(sampleRate, samplesPerBlock);

    mNoiseGate.prepare(sampleRate, samplesPerBlock);

    mSlotBuffers.setSize(kNumSlots, samplesPerBlock, false, true, true);
    mGateKey.setSize(1, samplesPerBlock, false, true, true);

    for (int i = 0; i < kNumSlots; ++i)
    {
        mPanLeftGain[static_cast<size_t>(i)].reset(sampleRate, kSmoothingSeconds);
        mPanRightGain[static_cast<size_t>(i)].reset(sampleRate, kSmoothingSeconds);
    }
    mMasterGain.reset(sampleRate, kSmoothingSeconds);

    updateLatency();
}

void NAMModelerProcessor::releaseResources()
{
    for (auto& slot : mSlots)
        slot.reset();
    mNoiseGate.reset();
}

bool NAMModelerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    // Mono in / stereo out is the signature use case: one guitar, two amps.
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

SlotParameters NAMModelerProcessor::readSlotParameters(int slotIndex) const
{
    const auto id = [slotIndex](const char* base) { return pid::slotParam(slotIndex, base); };

    SlotParameters params;
    params.enabled = boolParameter(mApvts, id(pid::enabled));
    params.inputTrimDb = parameterValue(mApvts, id(pid::inTrim));
    params.outputTrimDb = parameterValue(mApvts, id(pid::outTrim));
    params.phaseInvert = boolParameter(mApvts, id(pid::phase));
    params.eqEnabled = boolParameter(mApvts, id(pid::eqOn));
    params.bass = parameterValue(mApvts, id(pid::bass));
    params.mid = parameterValue(mApvts, id(pid::mid));
    params.treble = parameterValue(mApvts, id(pid::treble));
    params.outputMode = static_cast<OutputMode>(juce::roundToInt(parameterValue(mApvts, id(pid::outMode))));
    params.calibrateInput = boolParameter(mApvts, id(pid::calIn));
    params.calibrationDbu = parameterValue(mApvts, id(pid::calDbu));
    return params;
}

void NAMModelerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numInputChannels = getTotalNumInputChannels();
    const int numOutputChannels = getTotalNumOutputChannels();

    if (numSamples <= 0)
        return;

    // A host may hand us a bigger block than it promised; grow rather than overrun.
    if (mSlotBuffers.getNumSamples() < numSamples)
    {
        mSlotBuffers.setSize(kNumSlots, numSamples, false, true, true);
        mGateKey.setSize(1, numSamples, false, true, true);
    }

    const auto inputMode = static_cast<InputMode>(juce::roundToInt(parameterValue(mApvts, pid::inputMode)));

    // Build each slot's mono input.
    float* slotA = mSlotBuffers.getWritePointer(0);
    float* slotB = mSlotBuffers.getWritePointer(1);

    if (inputMode == InputMode::stereo && numInputChannels >= 2)
    {
        juce::FloatVectorOperations::copy(slotA, buffer.getReadPointer(0), numSamples);
        juce::FloatVectorOperations::copy(slotB, buffer.getReadPointer(1), numSamples);
    }
    else
    {
        // Mono mode: the guitar feeds both amps. Take the left/only channel
        // rather than summing, matching how interfaces present a DI.
        const float* source = numInputChannels > 0 ? buffer.getReadPointer(0) : nullptr;
        if (source != nullptr)
        {
            juce::FloatVectorOperations::copy(slotA, source, numSamples);
            juce::FloatVectorOperations::copy(slotB, source, numSamples);
        }
        else
        {
            juce::FloatVectorOperations::clear(slotA, numSamples);
            juce::FloatVectorOperations::clear(slotB, numSamples);
        }
    }

    // Input metering, taken before any processing.
    {
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax(peak, std::abs(slotA[i]));
        mInputLevel.store(peak, std::memory_order_relaxed);
    }

    // One gate, keyed from the input, applied after each model so that hiss
    // amplified by a high-gain capture gets shut down too.
    const bool gateEnabled = boolParameter(mApvts, pid::gateOn);
    if (gateEnabled)
    {
        float* key = mGateKey.getWritePointer(0);
        for (int i = 0; i < numSamples; ++i)
            key[i] = 0.5f * (slotA[i] + slotB[i]);
        mNoiseGate.trigger(key, numSamples, parameterValue(mApvts, pid::gateThresh));
    }

    // Solo/mute across the two slots.
    std::array<bool, kNumSlots> audible{};
    bool anySolo = false;
    for (int i = 0; i < kNumSlots; ++i)
        anySolo = anySolo || boolParameter(mApvts, pid::slotParam(i, pid::solo));

    for (int i = 0; i < kNumSlots; ++i)
    {
        const bool soloed = boolParameter(mApvts, pid::slotParam(i, pid::solo));
        const bool muted = boolParameter(mApvts, pid::slotParam(i, pid::mute));
        audible[static_cast<size_t>(i)] = !muted && (!anySolo || soloed);
    }

    // Run the amps.
    std::array<bool, kNumSlots> produced{};
    for (int i = 0; i < kNumSlots; ++i)
    {
        float* slotData = mSlotBuffers.getWritePointer(i);
        produced[static_cast<size_t>(i)] =
            mSlots[static_cast<size_t>(i)].process(slotData, numSamples, readSlotParameters(i));

        if (produced[static_cast<size_t>(i)] && gateEnabled)
            mNoiseGate.applyGain(slotData, numSamples);
    }

    // Pan and sum. Writing into the host buffer only after both amps have run
    // means a slot with no model contributes silence rather than dry signal.
    buffer.clear();

    const bool monoSum = boolParameter(mApvts, pid::monoSum);
    mMasterGain.setTargetValue(juce::Decibels::decibelsToGain(parameterValue(mApvts, pid::masterOut)));

    float* left = buffer.getWritePointer(0);
    float* right = numOutputChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < kNumSlots; ++i)
    {
        if (!produced[static_cast<size_t>(i)] || !audible[static_cast<size_t>(i)])
            continue;

        float panLeft = 0.0f, panRight = 0.0f;
        constantPowerPan(parameterValue(mApvts, pid::slotParam(i, pid::pan)), panLeft, panRight);
        mPanLeftGain[static_cast<size_t>(i)].setTargetValue(panLeft);
        mPanRightGain[static_cast<size_t>(i)].setTargetValue(panRight);

        const float* slotData = mSlotBuffers.getReadPointer(i);

        for (int s = 0; s < numSamples; ++s)
        {
            const float gainL = mPanLeftGain[static_cast<size_t>(i)].getNextValue();
            const float gainR = mPanRightGain[static_cast<size_t>(i)].getNextValue();
            left[s] += slotData[s] * gainL;
            if (right != nullptr)
                right[s] += slotData[s] * gainR;
        }
    }

    // Master gain, then optional mono collapse for compatibility checks.
    for (int s = 0; s < numSamples; ++s)
    {
        const float gain = mMasterGain.getNextValue();
        left[s] *= gain;
        if (right != nullptr)
            right[s] *= gain;
    }

    if (monoSum && right != nullptr)
    {
        for (int s = 0; s < numSamples; ++s)
        {
            const float summed = 0.5f * (left[s] + right[s]);
            left[s] = summed;
            right[s] = summed;
        }
    }

    // Output metering.
    mOutputLevel[0].store(buffer.getMagnitude(0, 0, numSamples), std::memory_order_relaxed);
    mOutputLevel[1].store(right != nullptr ? buffer.getMagnitude(1, 0, numSamples) : 0.0f,
                          std::memory_order_relaxed);

    // Any output channels beyond stereo are not part of the design.
    for (int channel = 2; channel < numOutputChannels; ++channel)
        buffer.clear(channel, 0, numSamples);
}

void NAMModelerProcessor::updateLatency()
{
    int latency = 0;
    for (auto& slot : mSlots)
        latency = juce::jmax(latency, slot.getLatencySamples());

    if (latency != mReportedLatency)
    {
        mReportedLatency = latency;
        setLatencySamples(latency);
    }
}

void NAMModelerProcessor::loadModel(int slotIndex, const juce::File& file)
{
    mLoader.loadFromFile(slotIndex, file);
}

void NAMModelerProcessor::clearModel(int slotIndex)
{
    mLoader.clearSlot(slotIndex);
}

ModelInfo NAMModelerProcessor::getModelInfo(int slotIndex) const
{
    if (!juce::isPositiveAndBelow(slotIndex, kNumSlots))
        return {};
    return mModelInfo[static_cast<size_t>(slotIndex)];
}

juce::String NAMModelerProcessor::getSlotError(int slotIndex) const
{
    if (!juce::isPositiveAndBelow(slotIndex, kNumSlots))
        return {};
    return mSlotErrors[static_cast<size_t>(slotIndex)];
}

void NAMModelerProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree root(state::kRootType);
    root.appendChild(mApvts.copyState(), nullptr);

    for (int i = 0; i < kNumSlots; ++i)
        root.appendChild(state::toValueTree(i, mModelInfo[static_cast<size_t>(i)]), nullptr);

    juce::MemoryOutputStream stream(destData, false);
    root.writeToStream(stream);
}

void NAMModelerProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
    const auto root = juce::ValueTree::readFromStream(stream);

    if (!root.isValid() || !root.hasType(state::kRootType))
        return;

    const auto parameters = root.getChildWithName(mApvts.state.getType());
    if (parameters.isValid())
        mApvts.replaceState(parameters);

    for (int child = 0; child < root.getNumChildren(); ++child)
    {
        const auto slotTree = root.getChild(child);
        if (!slotTree.hasType(state::kSlotType))
            continue;

        const int slotIndex = static_cast<int>(slotTree.getProperty(state::kSlotIndex, -1));
        if (!juce::isPositiveAndBelow(slotIndex, kNumSlots))
            continue;

        juce::String json, name, path;
        if (state::fromValueTree(slotTree, json, name, path))
            mLoader.loadFromJson(slotIndex, json, name, path);
        else
            mLoader.clearSlot(slotIndex);
    }
}

juce::AudioProcessorEditor* NAMModelerProcessor::createEditor()
{
    return new NAMModelerEditor(*this);
}

} // namespace nammodeler

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new nammodeler::NAMModelerProcessor();
}
