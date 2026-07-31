#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "blocks/BuiltInBlock.h"

namespace blockrig
{

/// Gain, pan, phase, and a channel-swap.
///
/// Exists so a rig never needs a paid plug-in to trim six decibels. It is also
/// the tool for the width problems this engine takes seriously: a phase invert
/// on one side, or a swap, is how you check what a stereo effect is really
/// doing.
///
/// Not width-neutral: panning and per-channel phase both change the
/// relationship between the sides, which is exactly the point.
class UtilityBlockProcessor final : public BuiltInBlockProcessor
{
public:
    static constexpr const char* kIdentifier = "utility";

    static juce::PluginDescription getBlockDescription();

    UtilityBlockProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::SmoothedValue<float> mGain;
    juce::SmoothedValue<float> mLeftGain, mRightGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UtilityBlockProcessor)
};

} // namespace blockrig
