#include "blocks/nam/NamBlockProcessor.h"

#include "state/PluginState.h"

namespace blockrig
{
namespace
{
constexpr int kVersionHint = 1;

juce::ParameterID makeId(const char* id)
{
    return juce::ParameterID{id, kVersionHint};
}

juce::NormalisableRange<float> dbRange(float lo, float hi)
{
    juce::NormalisableRange<float> range{lo, hi, 0.01f};
    range.setSkewForCentre(0.0f);
    return range;
}

std::unique_ptr<juce::AudioParameterFloat> gainParam(const char* id, const juce::String& name, float lo, float hi)
{
    return std::make_unique<juce::AudioParameterFloat>(
        makeId(id), name, dbRange(lo, hi), 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dB").withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 1) + " dB"; }));
}

std::unique_ptr<juce::AudioParameterFloat> toneParam(const char* id, const juce::String& name)
{
    return std::make_unique<juce::AudioParameterFloat>(
        makeId(id), name, juce::NormalisableRange<float>{0.0f, 10.0f, 0.01f}, 5.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 2); }));
}

float rawValue(const juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    if (auto* value = apvts.getRawParameterValue(id))
        return value->load(std::memory_order_relaxed);
    jassertfalse;
    return 0.0f;
}

bool boolValue(const juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    return rawValue(apvts, id) >= 0.5f;
}
} // namespace

juce::PluginDescription NamBlockProcessor::getBlockDescription()
{
    juce::PluginDescription description;
    description.name = "NAM";
    description.descriptiveName = "Neural Amp Modeler";
    description.pluginFormatName = kFormatName;
    description.category = "Amp";
    description.manufacturerName = "BlockRig";
    description.version = "1.0.0";
    description.fileOrIdentifier = kIdentifier;
    description.uniqueId = description.deprecatedUid = 0x4E414D31; // 'NAM1'
    description.isInstrument = false;
    description.numInputChannels = 2;
    description.numOutputChannels = 2;
    return description;
}

NamBlockProcessor::NamBlockProcessor()
    : juce::AudioPluginInstance(BusesProperties()
                                    .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , mApvts(*this, nullptr, "PARAMETERS", createLayout())
{
    mLoader.attachSlot(0, &mSlot);
    mLoader.attachSlot(1, &mSlotRight);

    mLoader.onLoadFinished = [this](int, nammodeler::ModelInfo info, juce::String error) {
        mModelInfo = std::move(info);
        mError = std::move(error);
        updateLatency();

        if (onModelStateChanged)
            onModelStateChanged();
    };
}

NamBlockProcessor::~NamBlockProcessor()
{
    mLoader.onLoadFinished = nullptr;
}

juce::AudioProcessorValueTreeState::ParameterLayout NamBlockProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(gainParam("in_trim", "Input Trim", -20.0f, 20.0f));
    layout.add(gainParam("out_trim", "Output Trim", -40.0f, 40.0f));

    layout.add(std::make_unique<juce::AudioParameterBool>(makeId("eq_on"), "EQ", true));
    layout.add(toneParam("bass", "Bass"));
    layout.add(toneParam("mid", "Mid"));
    layout.add(toneParam("treble", "Treble"));

    layout.add(std::make_unique<juce::AudioParameterBool>(makeId("gate_on"), "Gate", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        makeId("gate_thresh"), "Gate Threshold", juce::NormalisableRange<float>{-100.0f, 0.0f, 0.1f}, -80.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dB").withStringFromValueFunction([](float v, int) {
            return v <= -99.95f ? juce::String("Off") : juce::String(v, 1) + " dB";
        })));

    // The parameters a .nam model actually exposes.
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        makeId("out_mode"), "Output Mode", juce::StringArray{"Raw", "Normalized", "Calibrated"},
        static_cast<int>(nammodeler::OutputMode::normalized)));

    layout.add(std::make_unique<juce::AudioParameterBool>(makeId("cal_in"), "Calibrate Input", false));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        makeId("cal_dbu"), "Interface Level", juce::NormalisableRange<float>{-60.0f, 60.0f, 0.1f}, 12.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dBu").withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 1) + " dBu"; })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        makeId("slim"), "Model Size", juce::NormalisableRange<float>{0.0f, 1.0f, 0.01f}, 1.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction(
            [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; })));

    // On by default: an amp block that folds the rig to mono is a surprise, and
    // the second instance costs about what the first does (A2 is ~3.4% of a core).
    layout.add(std::make_unique<juce::AudioParameterBool>(makeId("stereo"), "True Stereo", true));

    return layout;
}

void NamBlockProcessor::fillInPluginDescription(juce::PluginDescription& description) const
{
    description = getBlockDescription();
}

void NamBlockProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    mLoader.setAudioConfiguration(sampleRate, samplesPerBlock);
    mSlot.prepare(sampleRate, samplesPerBlock);
    mSlotRight.prepare(sampleRate, samplesPerBlock);
    mGate.prepare(sampleRate, samplesPerBlock);
    mMono.setSize(1, samplesPerBlock, false, true, true);
    mRightScratch.setSize(1, samplesPerBlock, false, true, true);
    updateLatency();
}

void NamBlockProcessor::releaseResources()
{
    mSlot.reset();
    mSlotRight.reset();
    mGate.reset();
}

bool NamBlockProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

nammodeler::SlotParameters NamBlockProcessor::readParameters() const
{
    nammodeler::SlotParameters params;
    params.enabled = true; // the lane owns enable/bypass
    params.inputTrimDb = rawValue(mApvts, "in_trim");
    params.outputTrimDb = rawValue(mApvts, "out_trim");
    params.phaseInvert = false; // polarity is a lane concern, not the amp's
    params.eqEnabled = boolValue(mApvts, "eq_on");
    params.bass = rawValue(mApvts, "bass");
    params.mid = rawValue(mApvts, "mid");
    params.treble = rawValue(mApvts, "treble");
    params.outputMode = static_cast<nammodeler::OutputMode>(juce::roundToInt(rawValue(mApvts, "out_mode")));
    params.calibrateInput = boolValue(mApvts, "cal_in");
    params.calibrationDbu = rawValue(mApvts, "cal_dbu");
    return params;
}

void NamBlockProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numInputs = getTotalNumInputChannels();
    const int numOutputs = getTotalNumOutputChannels();

    if (numSamples <= 0)
        return;

    if (mMono.getNumSamples() < numSamples)
        mMono.setSize(1, numSamples, false, true, true);

    // NAM captures are mono. Collapse the lane's stereo down to feed the model.
    float* mono = mMono.getWritePointer(0);
    if (numInputs >= 2)
    {
        const auto* left = buffer.getReadPointer(0);
        const auto* right = buffer.getReadPointer(1);
        for (int i = 0; i < numSamples; ++i)
            mono[i] = 0.5f * (left[i] + right[i]);
    }
    else if (numInputs == 1)
    {
        juce::FloatVectorOperations::copy(mono, buffer.getReadPointer(0), numSamples);
    }
    else
    {
        juce::FloatVectorOperations::clear(mono, numSamples);
    }

    {
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax(peak, std::abs(mono[i]));
        mInputLevel.store(peak, std::memory_order_relaxed);
    }

    // Gate is keyed from the block's input but applied after the model, so hiss
    // amplified by a high-gain capture gets shut down too.
    const bool gateEnabled = boolValue(mApvts, "gate_on");
    if (gateEnabled)
        mGate.trigger(mono, numSamples, rawValue(mApvts, "gate_thresh"));

    const auto params = readParameters();
    const bool stereo = boolValue(mApvts, "stereo") && numInputs >= 2 && numOutputs >= 2;

    if (stereo)
    {
        // Two instances of the same capture, one per channel, so a stereo image
        // arriving here survives the amp instead of being folded to mono.
        float* right = mRightScratch.getWritePointer(0);
        juce::FloatVectorOperations::copy(mono, buffer.getReadPointer(0), numSamples);
        juce::FloatVectorOperations::copy(right, buffer.getReadPointer(1), numSamples);

        const bool leftProduced = mSlot.process(mono, numSamples, params);
        const bool rightProduced = mSlotRight.process(right, numSamples, params);

        if (!leftProduced && !rightProduced)
        {
            mOutputLevel.store(0.0f, std::memory_order_relaxed);
            return;
        }

        // The two slots load asynchronously, one after the other, so there is a
        // window - and a failure mode - where only one side has a model. Mirror
        // the amped side rather than emitting the dry signal on the other:
        // amp-left-dry-right is far more wrong than a moment of mono.
        if (leftProduced != rightProduced)
        {
            if (leftProduced)
                juce::FloatVectorOperations::copy(right, mono, numSamples);
            else
                juce::FloatVectorOperations::copy(mono, right, numSamples);
        }

        if (gateEnabled)
        {
            mGate.applyGain(mono, numSamples);
            mGate.applyGain(right, numSamples);
        }

        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax(peak, std::abs(mono[i]), std::abs(right[i]));
        mOutputLevel.store(peak, std::memory_order_relaxed);

        juce::FloatVectorOperations::copy(buffer.getWritePointer(0), mono, numSamples);
        juce::FloatVectorOperations::copy(buffer.getWritePointer(1), right, numSamples);

        for (int channel = 2; channel < numOutputs; ++channel)
            juce::FloatVectorOperations::copy(buffer.getWritePointer(channel), mono, numSamples);

        return;
    }

    const bool produced = mSlot.process(mono, numSamples, params);
    if (!produced)
    {
        // No model loaded: pass the lane's signal through untouched rather than
        // dropping it, since an empty amp block should not silence a rig.
        mOutputLevel.store(0.0f, std::memory_order_relaxed);
        return;
    }

    if (gateEnabled)
        mGate.applyGain(mono, numSamples);

    {
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax(peak, std::abs(mono[i]));
        mOutputLevel.store(peak, std::memory_order_relaxed);
    }

    for (int channel = 0; channel < numOutputs; ++channel)
        juce::FloatVectorOperations::copy(buffer.getWritePointer(channel), mono, numSamples);
}

void NamBlockProcessor::updateLatency()
{
    const int latency = mSlot.getLatencySamples();
    if (latency != mReportedLatency)
    {
        mReportedLatency = latency;
        setLatencySamples(latency);
    }
}

void NamBlockProcessor::loadModel(const juce::File& namFile)
{
    // Both instances get the same capture: they are the two channels of one amp,
    // not two different amps.
    mLoader.loadFromFile(0, namFile);
    mLoader.loadFromFile(1, namFile);
}

void NamBlockProcessor::loadModelFromJson(const juce::String& json, const juce::String& name,
                                          const juce::String& path)
{
    mLoader.loadFromJson(0, json, name, path);
    mLoader.loadFromJson(1, json, name, path);
}

void NamBlockProcessor::clearModel()
{
    mLoader.clearSlot(0);
    mLoader.clearSlot(1);
}

void NamBlockProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree root(nammodeler::state::kRootType);
    root.appendChild(mApvts.copyState(), nullptr);
    root.appendChild(nammodeler::state::toValueTree(0, mModelInfo), nullptr);

    juce::MemoryOutputStream stream(destData, false);
    root.writeToStream(stream);
}

void NamBlockProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
    const auto root = juce::ValueTree::readFromStream(stream);

    if (!root.isValid() || !root.hasType(nammodeler::state::kRootType))
        return;

    const auto parameters = root.getChildWithName(mApvts.state.getType());
    if (parameters.isValid())
        mApvts.replaceState(parameters);

    const auto slotTree = root.getChildWithName(nammodeler::state::kSlotType);
    juce::String json, name, path;

    if (nammodeler::state::fromValueTree(slotTree, json, name, path))
        loadModelFromJson(json, name, path);
    else
        clearModel();
}

} // namespace blockrig
