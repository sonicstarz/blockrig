#pragma once

#include <functional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/BlockCategories.h"

namespace blockrig
{
class PluginCatalog;

/// Chooses what to put in a lane slot (4d).
///
/// With ~860 plug-ins installed on a typical machine, a nested category menu is
/// a chore — so search is the primary path, a category chip row narrows by
/// kind, and the list filters as you type. Built-in blocks, starred favourites,
/// and recently used plug-ins are pinned above everything else, because those
/// are what people actually reach for.
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

    /// "All" plus one chip per category, coloured to the category system.
    /// Wraps onto a second line when the labels outgrow the popover.
    class ChipRow final : public juce::Component
    {
    public:
        std::function<void()> onChanged;

        /// -1 = All.
        int getSelected() const { return mSelected; }

        int getPreferredHeight(int width);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseMove(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;

    private:
        struct Chip
        {
            int category; ///< index into getAllCategories(), -1 = All
            juce::String label;
            juce::Rectangle<float> bounds;
        };

        void layoutChips(int width);

        std::vector<Chip> mChips;
        int mSelected = -1;
        int mHover = -1;
        int mLayoutWidth = 0;
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

    /// Starred blocks persist across sessions: one plug-in identifier per line
    /// in …/BlockRig/starred-blocks.txt, next to the favourites folder.
    static juce::StringArray& getStars();
    static juce::File getStarsFile();
    static bool isStarred(const juce::PluginDescription&);
    static void toggleStar(const juce::PluginDescription&);

    PluginCatalog& mCatalog;
    juce::TextEditor mSearch;
    ChipRow mChips;
    juce::ListBox mList{"blocks", this};

    std::vector<Entry> mEntries;
    int mTotalBlocks = 0;
    juce::String mFooterLeft;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockPicker)
};

} // namespace blockrig
