#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/AmpSlot.h"
#include "dsp/ModelLoader.h"
#include "dsp/NoiseGate.h"
#include "state/Parameters.h"

namespace nammodeler
{

class NAMModelerProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kNumSlots = 2;

    NAMModelerProcessor();
    ~NAMModelerProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return mApvts; }

    /// Editor-facing model management.
    void loadModel(int slotIndex, const juce::File& file);
    void clearModel(int slotIndex);
    ModelInfo getModelInfo(int slotIndex) const;
    juce::String getSlotError(int slotIndex) const;

    /// Called on the message thread whenever a slot's model or error changes.
    std::function<void()> onModelStateChanged;

    /// Peak levels for metering, updated from the audio thread.
    float getInputLevel() const { return mInputLevel.load(std::memory_order_relaxed); }
    float getOutputLevel(int channel) const
    {
        return mOutputLevel[static_cast<size_t>(juce::jlimit(0, 1, channel))].load(std::memory_order_relaxed);
    }

private:
    SlotParameters readSlotParameters(int slotIndex) const;
    void updateLatency();
    void pushModelToSlot(int slotIndex, const ModelInfo& info);

    juce::AudioProcessorValueTreeState mApvts;

    std::array<AmpSlot, kNumSlots> mSlots;
    ModelLoader mLoader;
    NoiseGate mNoiseGate;

    // Message-thread copies used by the editor and by state saving.
    std::array<ModelInfo, kNumSlots> mModelInfo;
    std::array<juce::String, kNumSlots> mSlotErrors;

    // Audio-thread scratch, sized in prepareToPlay.
    juce::AudioBuffer<float> mSlotBuffers; // one mono channel per slot
    juce::AudioBuffer<float> mGateKey;

    std::array<juce::SmoothedValue<float>, kNumSlots> mPanLeftGain;
    std::array<juce::SmoothedValue<float>, kNumSlots> mPanRightGain;
    juce::SmoothedValue<float> mMasterGain;

    std::atomic<float> mInputLevel{0.0f};
    std::array<std::atomic<float>, 2> mOutputLevel{};

    int mReportedLatency = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NAMModelerProcessor)
};

} // namespace nammodeler
