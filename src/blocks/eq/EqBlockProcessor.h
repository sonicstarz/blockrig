#pragma once

#include <juce_dsp/juce_dsp.h>

#include "blocks/BuiltInBlock.h"

namespace blockrig
{

/// Five-band EQ: high-pass, low shelf, two bells, high shelf, low-pass.
///
/// The shape a guitar rig actually reaches for — cut the mud, tame the fizz,
/// carve one problem frequency — rather than a full parametric console strip.
/// Both channels get identical filters, so it is width-neutral: mono in stays
/// mono, and everything downstream can keep negotiating mono-in.
class EqBlockProcessor final : public BuiltInBlockProcessor
                             , public WidthNeutralProcessor
{
public:
    static constexpr const char* kIdentifier = "eq";

    static juce::PluginDescription getBlockDescription();

    EqBlockProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateCoefficients();

    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    /// Six filters per channel, duplicated: a stereo ProcessorChain would run
    /// one set over an interleaved block, but per-channel filters keep the
    /// bypass-when-flat check simple and cost the same.
    struct Channel
    {
        Filter highPass, lowShelf, bell1, bell2, highShelf, lowPass;
    };

    Channel mChannels[2];
    double mSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqBlockProcessor)
};

} // namespace blockrig
