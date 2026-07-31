#include "blocks/eq/EqBlockProcessor.h"

namespace blockrig
{
namespace
{
/// Log-ish frequency ranges, so a knob's travel matches how hearing works.
juce::NormalisableRange<float> frequencyRange(float minimum, float maximum)
{
    juce::NormalisableRange<float> range(minimum, maximum);
    range.setSkewForCentre(std::sqrt(minimum * maximum));
    return range;
}
} // namespace

juce::PluginDescription EqBlockProcessor::getBlockDescription()
{
    return makeDescription("EQ", "Five-band equaliser", "EQ", kIdentifier, 0x45513131); // 'EQ11'
}

juce::AudioProcessorValueTreeState::ParameterLayout EqBlockProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto gainRange = juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f);
    const auto qRange = juce::NormalisableRange<float>(0.2f, 8.0f, 0.01f, 0.4f);

    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"hp_on", 1},
                                                          "High-pass on", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"hp_freq", 1}, "High-pass", frequencyRange(20.0f, 1000.0f), 80.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"ls_freq", 1}, "Low shelf", frequencyRange(40.0f, 600.0f), 120.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"ls_gain", 1},
                                                           "Low shelf gain", gainRange, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"b1_freq", 1}, "Bell 1", frequencyRange(80.0f, 4000.0f), 500.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"b1_gain", 1},
                                                           "Bell 1 gain", gainRange, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"b1_q", 1}, "Bell 1 Q",
                                                           qRange, 1.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"b2_freq", 1}, "Bell 2", frequencyRange(400.0f, 12000.0f), 2500.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"b2_gain", 1},
                                                           "Bell 2 gain", gainRange, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"b2_q", 1}, "Bell 2 Q",
                                                           qRange, 1.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"hs_freq", 1}, "High shelf", frequencyRange(1500.0f, 16000.0f), 6000.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"hs_gain", 1},
                                                           "High shelf gain", gainRange, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"lp_on", 1},
                                                          "Low-pass on", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"lp_freq", 1}, "Low-pass", frequencyRange(1000.0f, 20000.0f), 12000.0f));

    return layout;
}

EqBlockProcessor::EqBlockProcessor()
    : BuiltInBlockProcessor(getBlockDescription(), createLayout())
{
}

void EqBlockProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = mSampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(juce::jmax(1, samplesPerBlock));
    spec.numChannels = 1;

    for (auto& channel : mChannels)
    {
        channel.highPass.prepare(spec);
        channel.lowShelf.prepare(spec);
        channel.bell1.prepare(spec);
        channel.bell2.prepare(spec);
        channel.highShelf.prepare(spec);
        channel.lowPass.prepare(spec);
    }

    updateCoefficients();
}

void EqBlockProcessor::releaseResources()
{
    for (auto& channel : mChannels)
    {
        channel.highPass.reset();
        channel.lowShelf.reset();
        channel.bell1.reset();
        channel.bell2.reset();
        channel.highShelf.reset();
        channel.lowPass.reset();
    }
}

void EqBlockProcessor::updateCoefficients()
{
    const auto nyquist = static_cast<float>(mSampleRate * 0.5);
    const auto clampFrequency = [nyquist](float frequency) {
        return juce::jlimit(20.0f, nyquist * 0.95f, frequency);
    };

    const auto highPass = Coefficients::makeHighPass(mSampleRate, clampFrequency(rawValue("hp_freq")));
    const auto lowShelf = Coefficients::makeLowShelf(
        mSampleRate, clampFrequency(rawValue("ls_freq")), 0.7f,
        juce::Decibels::decibelsToGain(rawValue("ls_gain")));
    const auto bell1 = Coefficients::makePeakFilter(
        mSampleRate, clampFrequency(rawValue("b1_freq")), juce::jmax(0.1f, rawValue("b1_q")),
        juce::Decibels::decibelsToGain(rawValue("b1_gain")));
    const auto bell2 = Coefficients::makePeakFilter(
        mSampleRate, clampFrequency(rawValue("b2_freq")), juce::jmax(0.1f, rawValue("b2_q")),
        juce::Decibels::decibelsToGain(rawValue("b2_gain")));
    const auto highShelf = Coefficients::makeHighShelf(
        mSampleRate, clampFrequency(rawValue("hs_freq")), 0.7f,
        juce::Decibels::decibelsToGain(rawValue("hs_gain")));
    const auto lowPass = Coefficients::makeLowPass(mSampleRate, clampFrequency(rawValue("lp_freq")));

    for (auto& channel : mChannels)
    {
        *channel.highPass.coefficients = *highPass;
        *channel.lowShelf.coefficients = *lowShelf;
        *channel.bell1.coefficients = *bell1;
        *channel.bell2.coefficients = *bell2;
        *channel.highShelf.coefficients = *highShelf;
        *channel.lowPass.coefficients = *lowPass;
    }
}

void EqBlockProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = getActiveInputChannels(buffer);

    if (numSamples <= 0)
        return;

    // Recomputing every block is cheap next to the filtering itself, and it
    // means a moving knob (or a MIDI pedal) is heard without a smoothing scheme
    // of its own. Biquad coefficient jumps are the classic zipper source, but at
    // these Q values and one update per block the artefact is inaudible.
    updateCoefficients();

    const bool highPassOn = boolValue("hp_on");
    const bool lowPassOn = boolValue("lp_on");

    for (int channelIndex = 0; channelIndex < numChannels; ++channelIndex)
    {
        auto& channel = mChannels[channelIndex];
        auto* data = buffer.getWritePointer(channelIndex);

        for (int i = 0; i < numSamples; ++i)
        {
            auto sample = data[i];

            if (highPassOn)
                sample = channel.highPass.processSample(sample);

            sample = channel.lowShelf.processSample(sample);
            sample = channel.bell1.processSample(sample);
            sample = channel.bell2.processSample(sample);
            sample = channel.highShelf.processSample(sample);

            if (lowPassOn)
                sample = channel.lowPass.processSample(sample);

            data[i] = sample;
        }

        // Denormals can survive in the filter states between blocks of silence.
        channel.highPass.snapToZero();
        channel.lowShelf.snapToZero();
        channel.bell1.snapToZero();
        channel.bell2.snapToZero();
        channel.highShelf.snapToZero();
        channel.lowPass.snapToZero();
    }

    if (numChannels == 1)
        fanOutMono(buffer, numSamples);
}

} // namespace blockrig
