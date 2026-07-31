#pragma once

#include <atomic>
#include <functional>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

namespace blockrig
{
class BlockRigProcessor;

/// MIDI control for the rig: program changes recall snapshots, CCs drive any
/// parameter of any block plus the global actions, and mappings are learned by
/// wiggling the controller.
///
/// Mappings live in the rig file. Block parameters are rig-scoped by nature -
/// the block they point at only exists in that rig - and the globals ride along
/// so a rig is one self-contained file, pedals included.
///
/// Threading: standalone MIDI arrives on the system's MIDI thread; in a DAW it
/// arrives in processBlock. Parameter moves are applied wherever they land -
/// hosts drive setValueNotifyingHost from arbitrary threads and plug-ins must
/// cope - but anything structural (snapshots, tuner, mute) is posted to the
/// message thread.
class MidiEngine final : public juce::MidiInputCallback
{
public:
    explicit MidiEngine(BlockRigProcessor& processor);

    /// What a CC steers.
    struct Mapping
    {
        int cc = -1; ///< -1 while unlearned

        /// Non-empty for a block parameter target.
        juce::String blockUid;
        int parameterIndex = -1;

        /// Non-empty for a global target: "mute", "tuner", "tap",
        /// "snapshotNext", "snapshotPrev", "rigNext", "rigPrev".
        juce::String globalId;

        /// Buttons send 127/0; a toggle flips state on press instead of
        /// following the value, which is what a momentary footswitch needs.
        bool toggle = false;

        /// 0 = omni. Two pedals sending the same CC on different channels is
        /// normal on a shared MIDI chain.
        int channel = 0;

        /// Where the pedal's travel lands. An expression pedal that should only
        /// sweep a delay from 20% to 60% is a real request, and scaling it here
        /// beats asking every plug-in to have a range control.
        float minimum = 0.0f;
        float maximum = 1.0f;

        juce::String description; ///< human-readable target name for the UI
    };

    std::vector<Mapping> getMappings() const;
    void setMappings(std::vector<Mapping> mappings);
    void addMapping(Mapping mapping);
    void removeMapping(int index);

    /// Arms a mapping: the next CC that moves becomes its trigger.
    void armLearn(int mappingIndex);
    int getArmedIndex() const { return mLearnIndex.load(std::memory_order_relaxed); }

    /// Plugin build: the DAW's MIDI comes through processBlock.
    void processBuffer(const juce::MidiBuffer& buffer);

    /// Standalone: registered with the device manager for all inputs.
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override;

    /// Where program changes point. Snapshots is the floor-controller default;
    /// rigs suits a set where each song is its own rig.
    enum class ProgramTarget
    {
        snapshots,
        rigs
    };

    void setProgramTarget(ProgramTarget target) { mProgramTarget.store(static_cast<int>(target)); }
    ProgramTarget getProgramTarget() const
    {
        return static_cast<ProgramTarget>(mProgramTarget.load());
    }

    /// Follows MIDI clock when a source is sending it, so the rig's delays lock
    /// to whatever the band is running.
    void setFollowMidiClock(bool shouldFollow) { mFollowClock.store(shouldFollow); }
    bool getFollowMidiClock() const { return mFollowClock.load(); }
    /// True while clock pulses are actually arriving.
    bool isReceivingClock() const;

    /// Last controller seen, for the panel's activity readout — "is my pedal
    /// even reaching this app" should answer itself.
    struct Activity
    {
        int cc = -1;
        int value = 0;
        int channel = 0;
        juce::int64 timeMs = 0;
    };

    Activity getLastActivity() const;

    /// Message thread, fired after a learn completes or mappings change.
    std::function<void()> onMappingsChanged;

    /// Fired when a program change should select a rig rather than a snapshot.
    std::function<void(int program)> onRigProgramRequested;

    /// Message thread, fired for actions the UI owns (rig stepping).
    std::function<void(int direction)> onRigStepRequested;

    juce::ValueTree toValueTree() const;
    void restoreFrom(const juce::ValueTree& tree);

private:
    void handle(const juce::MidiMessage& message);
    void handleClock();
    void dispatch(int cc, int value, int channel);
    void applyGlobal(const Mapping& mapping, int value, bool pressed);

    BlockRigProcessor& mProcessor;

    mutable juce::CriticalSection mLock;
    std::vector<Mapping> mMappings;
    std::atomic<int> mLearnIndex{-1};
    std::atomic<int> mProgramTarget{static_cast<int>(ProgramTarget::snapshots)};
    std::atomic<bool> mFollowClock{true};

    /// Clock state, MIDI thread only except for the atomics.
    std::atomic<juce::int64> mLastClockMs{0};
    std::atomic<double> mClockBpm{0.0};
    double mClockIntervals[24] = {};
    int mClockPulseCount = 0;
    juce::int64 mLastPulseTicks = 0;

    std::atomic<int> mActivityCc{-1};
    std::atomic<int> mActivityValue{0};
    std::atomic<int> mActivityChannel{0};
    std::atomic<juce::int64> mActivityMs{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiEngine)
};

} // namespace blockrig
