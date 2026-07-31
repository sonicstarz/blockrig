#include "blocks/ir/IrBlockProcessor.h"

namespace blockrig
{

juce::PluginDescription IrBlockProcessor::getBlockDescription()
{
    return makeDescription("IR", "Impulse response loader", "Cabinet", kIdentifier, 0x49525231); // 'IRR1'
}

juce::AudioProcessorValueTreeState::ParameterLayout IrBlockProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mix", 1}, "Mix", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"out_trim", 1}, "Output",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));

    return layout;
}

IrBlockProcessor::IrBlockProcessor()
    : BuiltInBlockProcessor(getBlockDescription(), createLayout())
{
}

IrBlockProcessor::~IrBlockProcessor() = default;

void IrBlockProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    spec.maximumBlockSize = static_cast<juce::uint32>(juce::jmax(1, samplesPerBlock));
    spec.numChannels = 2;

    mConvolution.prepare(spec);

    // Re-arm whatever was loaded: prepare() resets the convolver, and a rig
    // restored before the first prepareToPlay would otherwise come up silent.
    if (const auto file = getIrFile(); file.existsAsFile())
        mConvolution.loadImpulseResponse(file, juce::dsp::Convolution::Stereo::yes,
                                         juce::dsp::Convolution::Trim::yes, 0,
                                         juce::dsp::Convolution::Normalise::yes);

    mMix.reset(spec.sampleRate, 0.02);
    mOutputGain.reset(spec.sampleRate, 0.02);
    mMix.setCurrentAndTargetValue(rawValue("mix"));
    mOutputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(rawValue("out_trim")));
}

void IrBlockProcessor::releaseResources()
{
    mConvolution.reset();
}

void IrBlockProcessor::loadIr(const juce::File& file)
{
    if (!file.existsAsFile())
        return;

    // Into the library first, then load from the library's copy: the original
    // may be in a Downloads folder that gets cleaned out.
    mLibrary->addIr(file);

    {
        const juce::ScopedLock lock(mFileLock);
        mIrFile = file;
    }

    mConvolution.loadImpulseResponse(file, juce::dsp::Convolution::Stereo::yes,
                                     juce::dsp::Convolution::Trim::yes, 0,
                                     juce::dsp::Convolution::Normalise::yes);
    mHasIr.store(true, std::memory_order_release);

    if (onIrChanged)
        onIrChanged();
}

void IrBlockProcessor::clearIr()
{
    mHasIr.store(false, std::memory_order_release);

    {
        const juce::ScopedLock lock(mFileLock);
        mIrFile = juce::File{};
    }

    mConvolution.reset();

    if (onIrChanged)
        onIrChanged();
}

juce::File IrBlockProcessor::getIrFile() const
{
    const juce::ScopedLock lock(mFileLock);
    return mIrFile;
}

juce::String IrBlockProcessor::getIrName() const
{
    const auto file = getIrFile();

    if (!file.existsAsFile())
        return {};

    // Strip the library's content-hash suffix if present.
    const auto stem = file.getFileNameWithoutExtension();
    const auto tail = stem.fromLastOccurrenceOf(".", false, false);
    const bool isHash = tail.length() == 8 && tail.containsOnly("0123456789abcdef");
    return isHash ? stem.upToLastOccurrenceOf(".", false, false) : stem;
}

void IrBlockProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = mApvts.copyState();
    // The path, not the audio: IRs live in the library, and a rig that embedded
    // every cabinet would balloon a DAW project for no benefit.
    state.setProperty("irFile", getIrFile().getFullPathName(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void IrBlockProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto state = juce::ValueTree::fromXml(*xml);
    mApvts.replaceState(state);

    const juce::File file(state.getProperty("irFile", "").toString());

    if (file.existsAsFile())
        loadIr(file);
    else
        clearIr();
}

void IrBlockProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || buffer.getNumChannels() <= 0)
        return;

    // Fed mono: give the convolver two real channels to work with, and the
    // result stays width-neutral because the same IR runs on both.
    if (getActiveInputChannels(buffer) == 1)
        fanOutMono(buffer, numSamples);

    const auto numChannels = juce::jmin(2, buffer.getNumChannels());

    // No IR: pass through. An empty cabinet block should not silence a rig.
    if (!mHasIr.load(std::memory_order_acquire))
        return;

    mMix.setTargetValue(juce::jlimit(0.0f, 1.0f, rawValue("mix")));
    mOutputGain.setTargetValue(juce::Decibels::decibelsToGain(rawValue("out_trim")));

    // Keep the dry signal for the mix, since the convolver works in place.
    if (mDry.getNumChannels() < numChannels || mDry.getNumSamples() < numSamples)
        mDry.setSize(juce::jmax(2, numChannels), juce::jmax(numSamples, 512), false, false, true);

    for (int channel = 0; channel < numChannels; ++channel)
        mDry.copyFrom(channel, 0, buffer, channel, 0, numSamples);

    juce::dsp::AudioBlock<float> block(buffer.getArrayOfWritePointers(),
                                       static_cast<size_t>(numChannels),
                                       static_cast<size_t>(numSamples));
    juce::dsp::ProcessContextReplacing<float> context(block);
    mConvolution.process(context);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto mix = mMix.getNextValue();
        const auto gain = mOutputGain.getNextValue();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto wet = buffer.getSample(channel, i);
            const auto dry = mDry.getSample(channel, i);
            buffer.setSample(channel, i, (dry * (1.0f - mix) + wet * mix) * gain);
        }
    }
}

} // namespace blockrig
