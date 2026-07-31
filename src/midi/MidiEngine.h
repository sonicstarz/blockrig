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

    /// Message thread, fired after a learn completes or mappings change.
    std::function<void()> onMappingsChanged;

    /// Message thread, fired for actions the UI owns (rig stepping).
    std::function<void(int direction)> onRigStepRequested;

    juce::ValueTree toValueTree() const;
    void restoreFrom(const juce::ValueTree& tree);

private:
    void handle(const juce::MidiMessage& message);
    void dispatch(int cc, int value);
    void applyGlobal(const Mapping& mapping, int value, bool pressed);

    BlockRigProcessor& mProcessor;

    mutable juce::CriticalSection mLock;
    std::vector<Mapping> mMappings;
    std::atomic<int> mLearnIndex{-1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiEngine)
};

} // namespace blockrig
