#pragma once

#include <functional>
#include <vector>

#include <juce_data_structures/juce_data_structures.h>

namespace blockrig
{
class BlockRigProcessor;

/// Undo for the rig, as a ring of whole serialized rigs.
///
/// Storing snapshots of everything rather than a command log because the rig
/// already serializes cheaply — the dirty checker does it every few seconds —
/// and a command log would need an inverse for every edit, including edits made
/// inside third-party plug-in windows we cannot see into.
///
/// The cost that matters is restoring, not storing: a full restore
/// re-instantiates every plug-in, which is seconds with a big rig. So restore
/// takes the fast path whenever the block set and order are unchanged and only
/// the settings differ — which is most undos — and rebuilds only when the
/// structure actually changed.
class UndoHistory final
{
public:
    explicit UndoHistory(BlockRigProcessor& processor);

    /// Records the rig's current state as an undo point, if it differs from the
    /// last one. Call BEFORE a structural edit, and on a debounce after
    /// parameter changes.
    void capture(const juce::String& description);

    bool canUndo() const { return mPosition > 0; }
    bool canRedo() const { return mPosition + 1 < static_cast<int>(mEntries.size()); }

    juce::String getUndoDescription() const;
    juce::String getRedoDescription() const;

    void undo();
    void redo();

    void clear();

    /// Fired after an undo or redo has been applied, so the UI can refresh.
    std::function<void()> onApplied;

    /// True while an undo/redo is being applied, so the debounced capture does
    /// not record the restoration as a new edit.
    bool isApplying() const { return mApplying; }

    static constexpr int kMaxEntries = 40;

private:
    struct Entry
    {
        juce::ValueTree tree;
        juce::String description;
    };

    void applyEntry(const Entry& entry);
    /// True when only settings differ, so plug-ins can be left alone.
    bool structureMatches(const juce::ValueTree& tree) const;
    void applySettingsOnly(const juce::ValueTree& tree);

    BlockRigProcessor& mProcessor;
    std::vector<Entry> mEntries;
    int mPosition = -1;
    bool mApplying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UndoHistory)
};

} // namespace blockrig
