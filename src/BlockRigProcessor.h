#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "host/BlockChain.h"
#include "host/PluginCatalog.h"
#include "host/Transport.h"

namespace blockrig
{

/// The host itself: owns the lane, the plug-in catalog, and the rig document.
///
/// The same processor backs both deployments. Standalone it is driven by an
/// AudioProcessorPlayer against a real device; inside a DAW it is the plug-in,
/// and the lane's ends are the host's buses instead of device channels.
class BlockRigProcessor final : public juce::AudioProcessor
{
public:
    BlockRigProcessor();
    ~BlockRigProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BlockRig"; }
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
    // Rig editing, driven by the lane UI. All message-thread only.

    BlockChain& getChain() { return mChain; }
    PluginCatalog& getCatalog() { return mCatalog; }
    Transport& getTransport() { return mTransport; }

    /// Instantiates a plug-in and inserts it at `position`. Asynchronous because
    /// AUv3 cannot be created synchronously, and because a slow plug-in must not
    /// freeze the UI. `onFinished` reports the new block's uid, or an error.
    void addBlock(const juce::PluginDescription& description, BlockPosition position,
                  std::function<void(juce::String uid, juce::String error)> onFinished = {});

    void removeBlock(const juce::String& uid);
    void moveBlock(const juce::String& uid, BlockPosition position);

    /// Splits a stage into two parallel rows (side A above, side B below) or
    /// collapses it back to one. This is how dual-amp rigs are built.
    void splitStage(int stageIndex);
    void mergeStage(int stageIndex);

    /// Called on the message thread whenever the lane changes.
    std::function<void()> onChainChanged;

    //==============================================================================
    // Input and output ends of the lane.

    enum class InputMode
    {
        mono = 0,
        stereo = 1
    };

    void setInputMode(InputMode mode) { mInputMode = mode; }
    InputMode getInputMode() const { return mInputMode; }

    void setInputGainDb(float gainDb);
    void setOutputGainDb(float gainDb);
    float getInputGainDb() const { return mInputGainDb; }
    float getOutputGainDb() const { return mOutputGainDb; }

    /// Master mute. Engaged at startup in the standalone app: it opens a live
    /// input straight into a live output, and on an interface that monitors its
    /// own output that is a feedback loop before the user has touched anything.
    /// Also the kill switch when a rig does start howling.
    void setMuted(bool shouldBeMuted);
    bool isMuted() const { return mMuted.load(std::memory_order_relaxed); }

    /// Plays a 440 Hz tone straight to the output for a couple of seconds,
    /// ignoring the chain and the mute. Answers "is anything reaching my
    /// speakers at all" without needing an instrument plugged in.
    void startTestTone();
    bool isTestTonePlaying() const { return mTestToneSamplesLeft.load(std::memory_order_relaxed) > 0; }

    /// How many times processBlock has run. Distinguishes "the audio callback
    /// never reaches us" from "the signal really is silent", which no level
    /// reading can tell you on its own.
    juce::int64 getProcessBlockCount() const { return mProcessBlockCount.load(std::memory_order_relaxed); }

    float getInputLevel() const { return mInputLevel.load(std::memory_order_relaxed); }
    float getOutputLevel() const { return mOutputLevel.load(std::memory_order_relaxed); }

private:
    void updateLatency();

    BlockChain mChain;
    PluginCatalog mCatalog;
    Transport mTransport;

    InputMode mInputMode = InputMode::mono;
    float mInputGainDb = 0.0f;
    float mOutputGainDb = 0.0f;

    juce::SmoothedValue<float> mInputGain;
    juce::SmoothedValue<float> mOutputGain;
    juce::SmoothedValue<float> mMuteGain;
    /// Not muted by default. The standalone app mutes itself at startup, because
    /// it is the one that opens a live input into a live output. Note that
    /// wrapperType is Undefined there - the app builds this processor directly
    /// rather than through a plug-in wrapper - so it cannot be used to tell the
    /// two deployments apart.
    std::atomic<bool> mMuted{false};

    std::atomic<juce::int64> mProcessBlockCount{0};
    std::atomic<int> mTestToneSamplesLeft{0};
    double mTestTonePhase = 0.0;

    std::atomic<float> mInputLevel{0.0f};
    std::atomic<float> mOutputLevel{0.0f};

    int mReportedLatency = 0;

    /// Polls block latencies, which plug-ins can change without notice.
    class LatencyPoller;
    std::unique_ptr<LatencyPoller> mLatencyPoller;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockRigProcessor)
};

} // namespace blockrig
