#include "blocks/utility/UtilityBlockProcessor.h"

namespace blockrig
{

juce::PluginDescription UtilityBlockProcessor::getBlockDescription()
{
    return makeDescription("Utility", "Gain, pan, phase", "Utility", kIdentifier, 0x55544C31); // 'UTL1'
}

juce::AudioProcessorValueTreeState::ParameterLayout UtilityBlockProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gain", 1}, "Gain",
        juce::NormalisableRange<float>(-60.0f, 24.0f, 0.1f), 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"pan", 1}, "Pan", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));

    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"invertL", 1},
                                                          "Invert left", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"invertR", 1},
                                                          "Invert right", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"swap", 1},
                                                          "Swap channels", false));

    // Sums the two channels to mono before pan places the result. This is what
    // lets one branch of a graph split behave like a dualMono row did in the
    // lane: summed to mono, then panned hard to one side. Without it a hard pan
    // only attenuates the far channel, leaving whatever was already on the near
    // one — which is not the same sound. See docs/19 §3.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"sumToMono", 1},
                                                          "Sum to mono", false));

    return layout;
}

UtilityBlockProcessor::UtilityBlockProcessor()
    : BuiltInBlockProcessor(getBlockDescription(), createLayout())
{
}

void UtilityBlockProcessor::prepareToPlay(double sampleRate, int)
{
    const auto rampSeconds = 0.02;
    mGain.reset(sampleRate, rampSeconds);
    mLeftGain.reset(sampleRate, rampSeconds);
    mRightGain.reset(sampleRate, rampSeconds);

    mGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(rawValue("gain")));
    mLeftGain.setCurrentAndTargetValue(1.0f);
    mRightGain.setCurrentAndTargetValue(1.0f);
}

void UtilityBlockProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto active = getActiveInputChannels(buffer);

    if (numSamples <= 0)
        return;

    // Fed mono, the pan still has to place the signal, so fan out FIRST and
    // treat both channels as real from there.
    if (active == 1)
        fanOutMono(buffer, numSamples);

    const auto numChannels = juce::jmin(2, buffer.getNumChannels());

    if (boolValue("swap") && numChannels >= 2)
        for (int i = 0; i < numSamples; ++i)
        {
            const auto left = buffer.getSample(0, i);
            buffer.setSample(0, i, buffer.getSample(1, i));
            buffer.setSample(1, i, left);
        }

    // Sum before pan, so a hard-panned branch carries the whole signal rather
    // than only what happened to be on that side.
    if (boolValue("sumToMono") && numChannels >= 2)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto summed = 0.5f * (buffer.getSample(0, i) + buffer.getSample(1, i));
            buffer.setSample(0, i, summed);
            buffer.setSample(1, i, summed);
        }
    }

    // Constant-power pan: -3 dB in the centre, so sweeping does not change how
    // loud the block feels.
    const auto pan = juce::jlimit(-1.0f, 1.0f, rawValue("pan"));
    const auto angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;

    mGain.setTargetValue(juce::Decibels::decibelsToGain(rawValue("gain")));
    mLeftGain.setTargetValue(std::cos(angle) * juce::MathConstants<float>::sqrt2);
    mRightGain.setTargetValue(std::sin(angle) * juce::MathConstants<float>::sqrt2);

    const auto invertLeft = boolValue("invertL") ? -1.0f : 1.0f;
    const auto invertRight = boolValue("invertR") ? -1.0f : 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = mGain.getNextValue();
        const auto left = mLeftGain.getNextValue();
        const auto right = mRightGain.getNextValue();

        buffer.setSample(0, i, buffer.getSample(0, i) * gain * left * invertLeft);

        if (numChannels >= 2)
            buffer.setSample(1, i, buffer.getSample(1, i) * gain * right * invertRight);
    }
}

} // namespace blockrig
