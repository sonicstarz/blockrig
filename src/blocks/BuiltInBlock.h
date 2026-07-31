#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "host/ProcessorTraits.h"

namespace blockrig
{

/// Boilerplate shared by BlockRig's own blocks.
///
/// An AudioPluginInstance has a long tail of members that a built-in block has
/// nothing interesting to say about — programs, MIDI, editors. Collecting them
/// here keeps each block file about its DSP. The NAM block predates this and
/// stays as it is; it has enough of its own to say.
///
/// Derived blocks provide a description, an APVTS layout, and process().
class BuiltInBlockProcessor : public juce::AudioPluginInstance
{
public:
    static constexpr const char* kFormatName = "BlockRig";

    BuiltInBlockProcessor(juce::PluginDescription description,
                          juce::AudioProcessorValueTreeState::ParameterLayout layout)
        : juce::AudioPluginInstance(BusesProperties()
                                        .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
        , mDescription(std::move(description))
        , mApvts(*this, nullptr, "PARAMETERS", std::move(layout))
    {
    }

    juce::AudioProcessorValueTreeState& getValueTreeState() { return mApvts; }

    //==============================================================================
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        const auto& out = layouts.getMainOutputChannelSet();
        const auto& in = layouts.getMainInputChannelSet();

        if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
            return false;

        return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return mDescription.name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override
    {
        if (auto xml = mApvts.copyState().createXml())
            copyXmlToBinary(*xml, destData);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        if (auto xml = getXmlFromBinary(data, sizeInBytes))
            mApvts.replaceState(juce::ValueTree::fromXml(*xml));
    }

    void fillInPluginDescription(juce::PluginDescription& description) const override
    {
        description = mDescription;
    }

protected:
    float rawValue(const char* id) const
    {
        if (auto* parameter = mApvts.getRawParameterValue(id))
            return parameter->load(std::memory_order_relaxed);
        return 0.0f;
    }

    bool boolValue(const char* id) const { return rawValue(id) >= 0.5f; }

    /// How many channels of `buffer` actually carry input.
    ///
    /// Negotiated mono in / stereo out means the buffer has two channels but
    /// only the first holds signal - the second is scratch space for the output.
    /// Processing it as if it were audio produces a silent right channel, which
    /// is exactly the mono-feed trap this engine has been bitten by before.
    int getActiveInputChannels(const juce::AudioBuffer<float>& buffer) const
    {
        return juce::jlimit(1, juce::jmin(2, buffer.getNumChannels()), getTotalNumInputChannels());
    }

    /// Fans a mono result out to the remaining output channels. Call at the end
    /// of process() when getActiveInputChannels() was 1.
    void fanOutMono(juce::AudioBuffer<float>& buffer, int numSamples) const
    {
        const auto outputs = juce::jmin(2, buffer.getNumChannels());

        for (int channel = 1; channel < outputs; ++channel)
            buffer.copyFrom(channel, 0, buffer, 0, 0, numSamples);
    }

    /// Builds a description with the fields every built-in shares.
    static juce::PluginDescription makeDescription(const juce::String& name,
                                                   const juce::String& descriptiveName,
                                                   const juce::String& category,
                                                   const juce::String& identifier, int uniqueId)
    {
        juce::PluginDescription description;
        description.name = name;
        description.descriptiveName = descriptiveName;
        description.pluginFormatName = kFormatName;
        description.category = category;
        description.manufacturerName = "BlockRig";
        description.version = "1.0.0";
        description.fileOrIdentifier = identifier;
        description.uniqueId = description.deprecatedUid = uniqueId;
        description.isInstrument = false;
        description.numInputChannels = 2;
        description.numOutputChannels = 2;
        return description;
    }

    juce::PluginDescription mDescription;
    juce::AudioProcessorValueTreeState mApvts;
};

} // namespace blockrig
