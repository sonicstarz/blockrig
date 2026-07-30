#pragma once

#include <functional>
#include <map>
#include <vector>

#include <juce_data_structures/juce_data_structures.h>

namespace blockrig
{
class BlockRigProcessor;

namespace snapshots
{

/// One saved scene: the state of the rig's controls at a moment, minus its
/// structure.
///
/// A snapshot deliberately cannot add, remove or reorder blocks — that is what
/// rigs are for. It captures each block's full state chunk (which for the NAM
/// includes the loaded capture), the tempo, and whether the tuner was up; each
/// of those is individually optional, chosen when the snapshot is created.
struct Snapshot
{
    juce::String name;

    bool includeTempo = true;
    bool includeTuner = true;

    /// Block uid -> that block's full state chunk. Only blocks that were ticked
    /// when the snapshot was saved appear here; applying skips uids that no
    /// longer exist, so deleting a block does not break old snapshots.
    std::map<juce::String, juce::MemoryBlock> blockStates;

    double bpm = 120.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    bool tunerActive = false;
};

/// The rig's snapshots, owned by the processor so they serialize with the rig.
class Bank final
{
public:
    std::vector<Snapshot>& getSnapshots() { return mSnapshots; }
    const std::vector<Snapshot>& getSnapshots() const { return mSnapshots; }

    /// Fills a snapshot's payload from the rig as it currently sounds. `uids`
    /// selects which blocks participate; flags select the extras.
    static Snapshot capture(BlockRigProcessor& processor, juce::String name,
                            const juce::StringArray& uids, bool includeTempo, bool includeTuner);

    /// Puts a snapshot's payload back into the rig. Missing blocks are skipped.
    /// Returns the names of blocks whose state was applied, for feedback.
    static juce::StringArray apply(BlockRigProcessor& processor, const Snapshot& snapshot);

    /// The strip highlights the last-applied snapshot; not persisted.
    int activeIndex = -1;

    /// Fired after apply() recalls the tuner flag, so the UI can open or close
    /// the tuner window to match. Wired by MainView.
    std::function<void(bool tunerShouldBeOpen)> onTunerRecalled;

    juce::ValueTree toValueTree() const;
    void restoreFrom(const juce::ValueTree& tree);

private:
    std::vector<Snapshot> mSnapshots;
};

} // namespace snapshots
} // namespace blockrig
