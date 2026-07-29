#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "BlockRigProcessor.h"

namespace blockrig
{
class PluginEditorWindows;

/// A tile in the lane. Draws its own state: selected, bypassed, editor-open,
/// error, and a live activity bar.
class BlockTile final : public juce::Component
                      , public juce::SettableTooltipClient
{
public:
    BlockTile(juce::String uid, juce::String name, juce::String subtitle);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    const juce::String& getUid() const { return mUid; }

    void setSelected(bool shouldBeSelected);
    void setBypassed(bool shouldBeBypassed);
    void setEditorOpen(bool isOpen);
    void setActivity(float level);
    void setLoad(float fractionOfBudget);

    bool isBypassed() const { return mBypassed; }

    std::function<void()> onSelect;
    std::function<void()> onToggleBypass;
    std::function<void()> onOpenEditor;
    std::function<void()> onShowMenu;
    /// Reports a drag in progress and, on release, the index the user dropped at.
    std::function<void(int targetIndex, bool dropped)> onDragToIndex;

private:
    juce::String mUid, mName, mSubtitle;
    bool mSelected = false;
    bool mBypassed = false;
    bool mEditorOpen = false;
    float mActivity = 0.0f;
    float mLoad = 0.0f;
    bool mDragging = false;
    juce::Rectangle<int> mHomeBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockTile)
};

/// The Input or Output end of the lane. A real, clickable block rather than a
/// setting in a dialog — guitarists expect the chain to have visible ends, and
/// it makes the standalone and in-DAW mental models identical.
class EndBlock final : public juce::Component
{
public:
    enum class Kind
    {
        input,
        output
    };

    EndBlock(Kind kind);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    void setLevel(float level);
    void setCaption(juce::String caption);
    void setSelected(bool shouldBeSelected);

    std::function<void()> onSelect;

private:
    Kind mKind;
    juce::String mCaption;
    float mLevel = 0.0f;
    bool mSelected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EndBlock)
};

/// Scrollable strip that holds the tiles and draws what sits *between* them:
/// the connectors, and the indicator showing where a dragged block would land.
class LaneContent final : public juce::Component
{
public:
    void paint(juce::Graphics&) override;

    void setConnectors(std::vector<juce::Rectangle<int>> connectors)
    {
        mConnectors = std::move(connectors);
        repaint();
    }

    void setDropIndicatorX(int x)
    {
        if (mDropIndicatorX == x)
            return;
        mDropIndicatorX = x;
        repaint();
    }

private:
    std::vector<juce::Rectangle<int>> mConnectors;
    int mDropIndicatorX = -1;
};

/// The pedalboard strip: Input at the far left, Output at the far right, blocks
/// in between, and a "+" to add another.
class LaneView final : public juce::Component
                     , private juce::Timer
{
public:
    LaneView(BlockRigProcessor& processor, PluginEditorWindows& editorWindows);
    ~LaneView() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /// Rebuilds tiles from the chain. Called whenever the lane changes.
    void refresh();

    void setInputCaption(const juce::String& caption) { mInputBlock.setCaption(caption); }
    void setOutputCaption(const juce::String& caption) { mOutputBlock.setCaption(caption); }

    juce::String getSelectedUid() const { return mSelectedUid; }

    /// Fires when selection changes, so the panel below can follow.
    std::function<void()> onSelectionChanged;
    /// Fires when the user selects an end block (empty uid means input/output).
    std::function<void(EndBlock::Kind)> onEndBlockSelected;

private:
    void timerCallback() override;
    void addBlockAt(int index);
    void showBlockMenu(const juce::String& uid);
    void selectBlock(const juce::String& uid);
    int indexForX(int x) const;
    int xForIndex(int index) const;

    BlockRigProcessor& mProcessor;
    PluginEditorWindows& mEditorWindows;

    EndBlock mInputBlock{EndBlock::Kind::input};
    EndBlock mOutputBlock{EndBlock::Kind::output};
    juce::TextButton mAddButton{"+"};

    std::vector<std::unique_ptr<BlockTile>> mTiles;
    juce::String mSelectedUid;

    /// Where a dragged tile would land, or -1 when nothing is being dragged.
    int mDropIndicatorIndex = -1;

    juce::Viewport mViewport;
    LaneContent mLaneContent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LaneView)
};

} // namespace blockrig
