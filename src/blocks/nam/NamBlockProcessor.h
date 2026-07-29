#pragma once

#include <atomic>
#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/AmpSlot.h"
#include "dsp/ModelLoader.h"
#include "dsp/NoiseGate.h"

namespace blockrig
{

/// The built-in amp block: one NAM voice, served to the chain as an
/// AudioPluginInstance so it sits alongside third-party VST3/AU blocks with no
/// special-casing anywhere in the engine, the schema, or the UI.
///
/// The DSP is the engine verified in the project's first incarnation
/// (dsp/AmpSlot + ResamplingNam + ToneStack + NoiseGate), reused unchanged. What
/// differs is scope: this is a single amp voice, because routing now belongs to
/// the lane. Two amps means two blocks.
class NamBlockProcessor final : public juce::AudioPluginInstance
{
public:
    /// Identifiers used by InternalBlockFormat and the rig schema.
    static constexpr const char* kFormatName = "BlockRig";
    static constexpr const char* kIdentifier = "nam";

    static juce::PluginDescription getBlockDescription();

    NamBlockProcessor();
    ~NamBlockProcessor() override;

    //==============================================================================
    // AudioProcessor

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return "NAM"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    //==============================================================================
    // AudioPluginInstance

    void fillInPluginDescription(juce::PluginDescription& description) const override;

    //==============================================================================
    // Model management, driven by the block's inline editor panel.

    void loadModel(const juce::File& namFile);
    void loadModelFromJson(const juce::String& json, const juce::String& name, const juce::String& path);
    void clearModel();

    nammodeler::ModelInfo getModelInfo() const { return mModelInfo; }
    juce::String getModelError() const { return mError; }

    /// Called on the message thread when the loaded model or error changes.
    std::function<void()> onModelStateChanged;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return mApvts; }

    float getInputLevel() const { return mInputLevel.load(std::memory_order_relaxed); }
    float getOutputLevel() const { return mOutputLevel.load(std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    nammodeler::SlotParameters readParameters() const;
    void updateLatency();

    juce::AudioProcessorValueTreeState mApvts;

    nammodeler::AmpSlot mSlot;
    nammodeler::NoiseGate mGate;
    nammodeler::ModelLoader mLoader;

    nammodeler::ModelInfo mModelInfo;
    juce::String mError;

    juce::AudioBuffer<float> mMono;
    std::atomic<float> mInputLevel{0.0f};
    std::atomic<float> mOutputLevel{0.0f};
    int mReportedLatency = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NamBlockProcessor)
};

} // namespace blockrig
