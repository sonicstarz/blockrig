#pragma once

#include <atomic>
#include <functional>

#include <juce_dsp/juce_dsp.h>

#include "blocks/BuiltInBlock.h"
#include "blocks/ir/IrLibrary.h"

namespace blockrig
{

/// Cabinet IR loader: the natural partner to the NAM block, since most captures
/// are of an amp without a speaker.
///
/// Zero-latency partitioned convolution, so it costs nothing in delay — a
/// latency-reporting convolver would push the whole rig's PDC out for no reason
/// at cabinet-IR lengths.
///
/// Width-neutral: the same IR runs on both channels, so a mono feed stays mono
/// and blocks downstream keep negotiating mono-in. (A true stereo-IR mode would
/// change that and would have to drop the marker.)
class IrBlockProcessor final : public BuiltInBlockProcessor
                             , public WidthNeutralProcessor
{
public:
    static constexpr const char* kIdentifier = "ir";

    static juce::PluginDescription getBlockDescription();

    IrBlockProcessor();
    ~IrBlockProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    //==============================================================================
    /// Loads an IR file. Joins the IR library automatically, like captures do.
    void loadIr(const juce::File& file);
    void clearIr();

    juce::File getIrFile() const;
    juce::String getIrName() const;

    IrLibrary& getIrLibrary() { return *mLibrary; }

    /// Message thread, when the loaded IR changes.
    std::function<void()> onIrChanged;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::dsp::Convolution mConvolution{juce::dsp::Convolution::Latency{0}};

    juce::SharedResourcePointer<IrLibrary> mLibrary;

    mutable juce::CriticalSection mFileLock;
    juce::File mIrFile;

    /// Set once an IR is in the convolver; until then the block passes through
    /// rather than silencing the rig.
    std::atomic<bool> mHasIr{false};

    juce::SmoothedValue<float> mMix, mOutputGain;
    /// The convolver works in place, so the dry signal is kept for the mix.
    juce::AudioBuffer<float> mDry;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrBlockProcessor)
};

} // namespace blockrig
