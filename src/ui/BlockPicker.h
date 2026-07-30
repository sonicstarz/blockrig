#pragma once

#include <functional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/BlockCategories.h"

namespace blockrig
{
class PluginCatalog;

/// Chooses what to put in a lane slot.
///
/// With ~860 plug-ins installed on a typical machine, a nested category menu is
/// a chore — so search is the primary path and the list is filtered as you type.
/// Built-in blocks and recently used plug-ins are pinned above everything else,
/// because those are what people actually reach for.
class BlockPicker final : public juce::Component
                        , private juce::TextEditor::Listener
                        , private juce::ListBoxModel
{
public:
    explicit BlockPicker(PluginCatalog& catalog);
    ~BlockPicker() override;

    /// Shows the picker in a callout next to `target`.
    static void show(PluginCatalog& catalog, juce::Component& target,
                     std::function<void(const juce::PluginDescription&)> onChosen);

    void resized() override;
    void paint(juce::Graphics&) override;

    std::function<void(const juce::PluginDescription&)> onChosen;
    std::function<void()> onDismiss;

private:
    struct Entry
    {
        juce::PluginDescription description;
        bool isBuiltIn = false;
        bool isRecent = false;
        juce::String sectionLabel; ///< non-empty when this row starts a section
        bool isHeader = false;
        BlockCategory category = BlockCategory::other;
    };

    void rebuildEntries();

    // TextEditor::Listener
    void textEditorTextChanged(juce::TextEditor&) override;
    void textEditorReturnKeyPressed(juce::TextEditor&) override;
    void textEditorEscapeKeyPressed(juce::TextEditor&) override;

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void returnKeyPressed(int lastRowSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;

    void chooseRow(int row);

    /// Remembers the last few choices across pickers within a session.
    static juce::Array<juce::PluginDescription>& getRecents();

    PluginCatalog& mCatalog;
    juce::TextEditor mSearch;
    juce::ListBox mList{"blocks", this};
    juce::Label mHint;

    std::vector<Entry> mEntries;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockPicker)
};

} // namespace blockrig
