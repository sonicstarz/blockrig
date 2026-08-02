#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "BlockRigProcessor.h"

namespace blockrig
{

/// The row of snapshot chips under the header.
///
/// Two modes, deliberately explicit: normally clicking a chip APPLIES that
/// snapshot; in edit mode clicking a chip asks to OVERWRITE it with the rig's
/// current settings. One mode where clicks change the rig and another where
/// clicks change the snapshot — mixing those behind a modifier key is how
/// people destroy a scene they meant to recall.
class SnapshotStrip final : public juce::Component
{
public:
    explicit SnapshotStrip(BlockRigProcessor& processor);
    /// Out of line: Chip is incomplete here and unique_ptr needs its size.
    ~SnapshotStrip() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void refresh();

    static constexpr int kHeight = 56;

    /// MainView owns window creation; the strip only asks.
    std::function<void(std::unique_ptr<juce::Component> panel, juce::String title, int width, int height)>
        openPanel;

    /// Anything that changes the bank marks the rig dirty.
    std::function<void()> onBankChanged;

    /// Applying a snapshot may need the tuner opened or closed.
    std::function<void(bool tunerShouldBeOpen)> onTunerRecalled;

private:
    class Chip;
    class AddPanel;

    void rebuildChips();
    void chipClicked(int index);
    void showChipMenu(int index);
    void applySnapshot(int index);
    void overwriteSnapshot(int index);
    void showAddPanel();

    BlockRigProcessor& mProcessor;

    juce::TextButton mAddButton{"+"};
    juce::TextButton mEditToggle{"Edit"};
    std::vector<std::unique_ptr<Chip>> mChips;
    bool mEditMode = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SnapshotStrip)
};

} // namespace blockrig
