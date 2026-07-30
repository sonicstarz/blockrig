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
    /// "A" or "B" on a split stage; empty when the stage has a single path.
    void setRowLabel(juce::String label);

    bool isBypassed() const { return mBypassed; }

    std::function<void()> onSelect;
    std::function<void()> onToggleBypass;
    std::function<void()> onOpenEditor;
    std::function<void()> onShowMenu;
    /// Reports a drag in progress and, on release, where it was dropped. A point
    /// rather than an x, because a split stage means the row matters too.
    std::function<void(juce::Point<int> pointInLane, bool dropped)> onDragTo;

private:
    juce::String mUid, mName, mSubtitle;
    bool mSelected = false;
    bool mBypassed = false;
    bool mEditorOpen = false;
    float mActivity = 0.0f;
    float mLoad = 0.0f;
    bool mDragging = false;
    juce::Rectangle<int> mHomeBounds;
    juce::String mRowLabel;

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

    void setDropIndicator(int x, int row, int totalRows)
    {
        if (mDropIndicatorX == x && mDropRow == row && mDropTotalRows == totalRows)
            return;
        mDropIndicatorX = x;
        mDropRow = row;
        mDropTotalRows = juce::jmax(1, totalRows);
        repaint();
    }

private:
    std::vector<juce::Rectangle<int>> mConnectors;
    int mDropIndicatorX = -1;
    int mDropRow = 0;
    int mDropTotalRows = 1;
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
    void addBlockAt(BlockPosition position, juce::Component& near);
    void showBlockMenu(const juce::String& uid);
    void showStageMenu(int stageIndex, juce::Component& near);
    void selectBlock(const juce::String& uid);

    /// Which slot the given lane-space point corresponds to.
    BlockPosition positionForPoint(juce::Point<int> point) const;
    int xForPosition(BlockPosition position) const;
    juce::Rectangle<int> boundsForStage(int stageIndex) const;
    int rowHeight() const;

    BlockRigProcessor& mProcessor;
    PluginEditorWindows& mEditorWindows;

    EndBlock mInputBlock{EndBlock::Kind::input};
    EndBlock mOutputBlock{EndBlock::Kind::output};
    juce::TextButton mAddButton{"+"};

    /// Cached stage geometry, so drag hit-testing and connector drawing agree.
    struct StageGeometry
    {
        int x = 0;
        int width = 0;
        int rows = 1;
    };

    std::vector<StageGeometry> mStageGeometry;

    /// A tile plus where it sits, so hit-testing a drag can work out both the
    /// stage and the row under the mouse.
    struct PlacedTile
    {
        std::unique_ptr<BlockTile> tile;
        BlockPosition position;
    };

    std::vector<PlacedTile> mTiles;

    /// One "+" per stage row, plus a trailing one for appending a new stage.
    std::vector<std::unique_ptr<juce::TextButton>> mRowAddButtons;

    juce::String mSelectedUid;
    BlockPosition mDropPosition;

    juce::Viewport mViewport;
    LaneContent mLaneContent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LaneView)
};

} // namespace blockrig
