#include "ui/LaneView.h"

#include "blocks/nam/NamBlockProcessor.h"
#include "ui/BlockPicker.h"
#include "ui/PluginEditorWindows.h"
#include "ui/Theme.h"

namespace blockrig
{
namespace
{
constexpr int kDragThreshold = 6;

/// Trims a plug-in name to something that fits a tile without looking chopped.
juce::String shortenName(const juce::String& name)
{
    auto trimmed = name.trim();

    // Manufacturer prefixes eat the width that matters ("bx_", "Waves ", ...).
    if (trimmed.length() > 18)
        trimmed = trimmed.substring(0, 17) + juce::String::charToString(0x2026);

    return trimmed;
}
} // namespace

//==============================================================================
BlockTile::BlockTile(juce::String uid, juce::String name, juce::String subtitle)
    : mUid(std::move(uid))
    , mName(std::move(name))
    , mSubtitle(std::move(subtitle))
{
    setTooltip(mName + (mSubtitle.isNotEmpty() ? " — " + mSubtitle : juce::String()));
}

void BlockTile::setSelected(bool shouldBeSelected)
{
    if (mSelected == shouldBeSelected)
        return;
    mSelected = shouldBeSelected;
    repaint();
}

void BlockTile::setBypassed(bool shouldBeBypassed)
{
    if (mBypassed == shouldBeBypassed)
        return;
    mBypassed = shouldBeBypassed;
    repaint();
}

void BlockTile::setEditorOpen(bool isOpen)
{
    if (mEditorOpen == isOpen)
        return;
    mEditorOpen = isOpen;
    repaint();
}

void BlockTile::setActivity(float level)
{
    // Compare in meter space: a 0.01 linear threshold is enormous down at the
    // levels a guitar actually produces.
    if (std::abs(theme::levelToMeterPosition(mActivity) - theme::levelToMeterPosition(level)) < 0.004f)
        return;
    mActivity = level;
    repaint();
}

void BlockTile::setLoad(float fractionOfBudget)
{
    mLoad = fractionOfBudget;
}

void BlockTile::setRowLabel(juce::String label)
{
    if (mRowLabel == label)
        return;
    mRowLabel = std::move(label);
    repaint();
}

void BlockTile::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);

    g.setColour(mBypassed ? theme::colours::panel : theme::colours::panelRaised);
    g.fillRoundedRectangle(bounds, theme::metrics::cornerRadius);

    g.setColour(mSelected ? theme::colours::accent : theme::colours::outline);
    g.drawRoundedRectangle(bounds, theme::metrics::cornerRadius, mSelected ? 1.8f : 1.0f);

    auto content = bounds.reduced(9.0f, 8.0f);

    // Bypass LED, top-left: click target and status in one.
    const juce::Rectangle<float> led{content.getX(), content.getY() + 1.0f, 8.0f, 8.0f};
    g.setColour(mBypassed ? theme::colours::textFaint : theme::colours::good);
    g.fillEllipse(led);

    if (mEditorOpen)
    {
        // Small marker so it is obvious which blocks have windows open.
        g.setColour(theme::colours::accent);
        g.fillEllipse(content.getRight() - 8.0f, content.getY() + 1.0f, 8.0f, 8.0f);
    }

    // Which side of a split this block is on.
    if (mRowLabel.isNotEmpty())
    {
        g.setColour(theme::colours::accent.withAlpha(0.9f));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(mRowLabel, juce::Rectangle<float>(content.getX() + 13.0f, content.getY(), 16.0f, 11.0f),
                   juce::Justification::centredLeft, false);
    }

    content.removeFromTop(14.0f);

    g.setColour(mBypassed ? theme::colours::textFaint : theme::colours::text);
    g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    g.drawFittedText(mName, content.removeFromTop(30.0f).toNearestInt(), juce::Justification::topLeft, 2, 0.85f);

    g.setColour(theme::colours::textFaint);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText(mSubtitle, content.removeFromTop(13.0f), juce::Justification::topLeft, true);

    // Output level of this block. Deliberately audio, not CPU: a bar on a block
    // reads as signal, and showing load here was actively misleading.
    theme::drawLevelMeter(g, content.removeFromBottom(5.0f), mBypassed ? 0.0f : mActivity);
}

void BlockTile::mouseDown(const juce::MouseEvent& event)
{
    if (onSelect)
        onSelect();

    // The LED area toggles bypass without selecting anything else.
    if (event.x < 22 && event.y < 24)
    {
        if (onToggleBypass)
            onToggleBypass();
        return;
    }

    if (event.mods.isPopupMenu())
        if (onShowMenu)
            onShowMenu();
}

void BlockTile::mouseDrag(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
        return;

    if (!mDragging && event.getDistanceFromDragStart() < kDragThreshold)
        return;

    if (!mDragging)
    {
        mDragging = true;
        mHomeBounds = getBounds();
        // Lift it above its neighbours so it is clearly the thing being moved.
        toFront(false);
        setAlpha(0.85f);
    }

    // Follow the mouse horizontally: without the tile actually moving, dragging
    // feels broken even when the reorder works.
    // Follows both axes: horizontally to reorder, vertically to move between the
    // A and B rows of a split.
    auto moved = mHomeBounds;
    moved.setPosition(mHomeBounds.getX() + event.getDistanceFromDragStartX(),
                      mHomeBounds.getY() + event.getDistanceFromDragStartY());
    setBounds(moved);

    if (onDragTo)
        onDragTo({getBounds().getCentreX(), getBounds().getCentreY()}, false);
}

void BlockTile::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    if (!mDragging)
        return;

    mDragging = false;
    setAlpha(1.0f);

    const juce::Point<int> centre{getBounds().getCentreX(), getBounds().getCentreY()};
    setBounds(mHomeBounds); // the parent will lay us out properly on refresh

    if (onDragTo)
        onDragTo(centre, true);
}

void BlockTile::mouseDoubleClick(const juce::MouseEvent&)
{
    if (onOpenEditor)
        onOpenEditor();
}

//==============================================================================
EndBlock::EndBlock(Kind kind)
    : mKind(kind)
{
}

void EndBlock::setLevel(float level)
{
    if (std::abs(theme::levelToMeterPosition(mLevel) - theme::levelToMeterPosition(level)) < 0.004f)
        return;
    mLevel = level;
    repaint();
}

void EndBlock::setCaption(juce::String caption)
{
    if (mCaption == caption)
        return;
    mCaption = std::move(caption);
    repaint();
}

void EndBlock::setSelected(bool shouldBeSelected)
{
    if (mSelected == shouldBeSelected)
        return;
    mSelected = shouldBeSelected;
    repaint();
}

void EndBlock::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);

    g.setColour(theme::colours::panel);
    g.fillRoundedRectangle(bounds, theme::metrics::cornerRadius);
    g.setColour(mSelected ? theme::colours::accent : theme::colours::outline);
    g.drawRoundedRectangle(bounds, theme::metrics::cornerRadius, mSelected ? 1.8f : 1.0f);

    auto content = bounds.reduced(9.0f, 8.0f);

    g.setColour(theme::colours::textDim);
    g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    g.drawText(mKind == Kind::input ? "INPUT" : "OUTPUT", content.removeFromTop(13.0f),
               juce::Justification::topLeft, false);

    content.removeFromTop(4.0f);
    g.setColour(theme::colours::text);
    g.setFont(juce::FontOptions(11.5f));
    g.drawFittedText(mCaption, content.removeFromTop(28.0f).toNearestInt(), juce::Justification::topLeft, 2, 0.9f);

    theme::drawLevelMeter(g, content.removeFromBottom(5.0f), mLevel);
}

void EndBlock::mouseDown(const juce::MouseEvent&)
{
    if (onSelect)
        onSelect();
}

//==============================================================================
LaneView::LaneView(BlockRigProcessor& processor, PluginEditorWindows& editorWindows)
    : mProcessor(processor)
    , mEditorWindows(editorWindows)
{
    mViewport.setViewedComponent(&mLaneContent, false);
    mViewport.setScrollBarsShown(false, true, false, true);
    addAndMakeVisible(mViewport);

    mLaneContent.addAndMakeVisible(mInputBlock);
    mLaneContent.addAndMakeVisible(mOutputBlock);
    mLaneContent.addAndMakeVisible(mAddButton);

    mInputBlock.onSelect = [this] {
        selectBlock({});
        if (onEndBlockSelected)
            onEndBlockSelected(EndBlock::Kind::input);
    };

    mOutputBlock.onSelect = [this] {
        selectBlock({});
        if (onEndBlockSelected)
            onEndBlockSelected(EndBlock::Kind::output);
    };

    mAddButton.onClick = [this] {
        addBlockAt(BlockPosition{mProcessor.getChain().getNumStages(), 0, 0}, mAddButton);
    };
    mAddButton.setTooltip("Add a block at the end of the chain");

    mEditorWindows.onWindowClosed = [this](juce::String) { refresh(); };

    refresh();
    startTimerHz(15);
}

LaneView::~LaneView()
{
    stopTimer();
    mEditorWindows.onWindowClosed = nullptr;
}

void LaneView::refresh()
{
    mTiles.clear();
    mRowAddButtons.clear();

    auto& chain = mProcessor.getChain();

    for (int stageIndex = 0; stageIndex < chain.getNumStages(); ++stageIndex)
    {
        const int rows = chain.getNumRows(stageIndex);

        for (int rowIndex = 0; rowIndex < rows; ++rowIndex)
        {
            const auto blocks = chain.getBlocksInRow(stageIndex, rowIndex);

            for (int index = 0; index < static_cast<int>(blocks.size()); ++index)
            {
                auto* block = blocks[static_cast<size_t>(index)];
                auto* plugin = block->getPlugin();

                // Subtitle carries what the user needs at a glance: for the NAM
                // block the loaded capture, for anything else who made it.
                juce::String subtitle;
                if (plugin != nullptr)
                {
                    if (auto* nam = dynamic_cast<NamBlockProcessor*>(plugin))
                    {
                        const auto info = nam->getModelInfo();
                        subtitle = info.json.isNotEmpty() ? info.name : juce::String("no capture");
                    }
                    else
                    {
                        juce::PluginDescription description;
                        plugin->fillInPluginDescription(description);
                        subtitle = description.manufacturerName;
                    }

                    const auto latency = block->getLatencySamples();
                    if (latency > 0)
                        subtitle += "  " + juce::String(latency) + " smp";
                }

                auto tile = std::make_unique<BlockTile>(block->getUid(), shortenName(block->getDisplayName()),
                                                       subtitle);
                const auto uid = block->getUid();

                tile->setBypassed(block->isBypassed());
                tile->setEditorOpen(mEditorWindows.isOpen(uid));
                tile->setSelected(uid == mSelectedUid);
                tile->setRowLabel(rows > 1 ? (rowIndex == 0 ? "A" : "B") : juce::String());

                tile->onSelect = [this, uid] { selectBlock(uid); };

                tile->onToggleBypass = [this, uid] {
                    if (auto* target = mProcessor.getChain().getBlockByUid(uid))
                    {
                        target->setBypassed(!target->isBypassed());
                        refresh();
                    }
                };

                tile->onOpenEditor = [this, uid] {
                    if (auto* target = mProcessor.getChain().getBlockByUid(uid))
                        if (auto* pluginToShow = target->getPlugin())
                        {
                            mEditorWindows.show(uid, *pluginToShow);
                            refresh();
                        }
                };

                tile->onShowMenu = [this, uid] { showBlockMenu(uid); };

                tile->onDragTo = [this, uid](juce::Point<int> pointInLane, bool dropped) {
                    const auto target = positionForPoint(pointInLane);

                    if (!dropped)
                    {
                        mDropPosition = target;
                        mLaneContent.setDropIndicator(xForPosition(target), target.row,
                                                      mProcessor.getChain().getNumRows(target.stage));
                        return;
                    }

                    mLaneContent.setDropIndicator(-1, 0, 1);
                    mProcessor.moveBlock(uid, target);
                    refresh();
                };

                mLaneContent.addAndMakeVisible(*tile);
                mTiles.push_back({std::move(tile), BlockPosition{stageIndex, rowIndex, index}});
            }

            // One "+" at the end of each row, so a split can be filled per side.
            auto addButton = std::make_unique<juce::TextButton>("+");
            const BlockPosition appendHere{stageIndex, rowIndex, static_cast<int>(blocks.size())};
            auto* buttonPtr = addButton.get();
            addButton->setTooltip(rows > 1 ? juce::String("Add a block to side ")
                                                 + (rowIndex == 0 ? "A" : "B")
                                           : juce::String("Add a block here"));
            addButton->onClick = [this, appendHere, buttonPtr] { addBlockAt(appendHere, *buttonPtr); };
            mLaneContent.addAndMakeVisible(*addButton);
            mRowAddButtons.push_back(std::move(addButton));
        }
    }

    resized();
    mLaneContent.repaint();
}

int LaneView::getPreferredHeight() const
{
    auto& chain = mProcessor.getChain();

    int maxRows = 1;
    for (int stageIndex = 0; stageIndex < chain.getNumStages(); ++stageIndex)
        maxRows = juce::jmax(maxRows, chain.getNumRows(stageIndex));

    // Room for the rows plus a little breathing space and the scrollbar.
    return maxRows * theme::metrics::blockHeight + 2 * theme::metrics::gap + 10;
}

int LaneView::rowHeight() const
{
    return theme::metrics::blockHeight;
}

juce::Rectangle<int> LaneView::boundsForStage(int stageIndex) const
{
    if (!juce::isPositiveAndBelow(stageIndex, static_cast<int>(mStageGeometry.size())))
        return {};

    const auto& geometry = mStageGeometry[static_cast<size_t>(stageIndex)];
    return {geometry.x, 0, geometry.width, geometry.rows * rowHeight()};
}

int LaneView::xForPosition(BlockPosition position) const
{
    // Left edge of the slot the block would occupy.
    const int slotWidth = theme::metrics::blockWidth + theme::metrics::arrowWidth;

    if (juce::isPositiveAndBelow(position.stage, static_cast<int>(mStageGeometry.size())))
    {
        const auto& geometry = mStageGeometry[static_cast<size_t>(position.stage)];
        return geometry.x + position.index * slotWidth - theme::metrics::arrowWidth / 2;
    }

    // Past the end: after the last stage.
    if (!mStageGeometry.empty())
    {
        const auto& last = mStageGeometry.back();
        return last.x + last.width + theme::metrics::arrowWidth / 2;
    }

    return theme::metrics::endBlockWidth + theme::metrics::arrowWidth / 2;
}

BlockPosition LaneView::positionForPoint(juce::Point<int> point) const
{
    const int slotWidth = theme::metrics::blockWidth + theme::metrics::arrowWidth;

    // Which stage is the mouse over (or nearest)?
    int stageIndex = static_cast<int>(mStageGeometry.size());

    for (size_t i = 0; i < mStageGeometry.size(); ++i)
    {
        const auto& geometry = mStageGeometry[i];

        if (point.x < geometry.x + geometry.width + theme::metrics::arrowWidth / 2)
        {
            stageIndex = static_cast<int>(i);
            break;
        }
    }

    BlockPosition position;
    position.stage = stageIndex;

    if (!juce::isPositiveAndBelow(stageIndex, static_cast<int>(mStageGeometry.size())))
        return position; // a new stage at the end

    const auto& geometry = mStageGeometry[static_cast<size_t>(stageIndex)];

    // Which row: rows stack vertically, so this is simply which band we are in.
    const int laneTop = juce::jmax(0, (getHeight() - geometry.rows * rowHeight()) / 2);
    const int relativeY = point.y - laneTop;
    position.row = juce::jlimit(0, geometry.rows - 1, relativeY / juce::jmax(1, rowHeight()));

    position.index = juce::jmax(0, (point.x - geometry.x + slotWidth / 2) / slotWidth);

    const auto blocks = mProcessor.getChain().getBlocksInRow(position.stage, position.row);
    position.index = juce::jlimit(0, static_cast<int>(blocks.size()), position.index);

    return position;
}

void LaneView::selectBlock(const juce::String& uid)
{
    if (mSelectedUid == uid)
        return;

    mSelectedUid = uid;

    for (auto& placed : mTiles)
        placed.tile->setSelected(placed.tile->getUid() == uid);

    mInputBlock.setSelected(false);
    mOutputBlock.setSelected(false);

    if (onSelectionChanged)
        onSelectionChanged();
}

void LaneView::addBlockAt(BlockPosition position, juce::Component& near)
{
    BlockPicker::show(mProcessor.getCatalog(), near,
                      [this, position](const juce::PluginDescription& description) {
                          mProcessor.addBlock(description, position,
                                              [this](juce::String uid, juce::String error) {
                                                  if (error.isNotEmpty())
                                                  {
                                                      juce::NativeMessageBox::showAsync(
                                                          juce::MessageBoxOptions()
                                                              .withIconType(juce::MessageBoxIconType::WarningIcon)
                                                              .withTitle("Could not add block")
                                                              .withMessage(error)
                                                              .withButton("OK"),
                                                          nullptr);
                                                      return;
                                                  }

                                                  refresh();
                                                  selectBlock(uid);
                                              });
                      });
}

void LaneView::showStageMenu(int stageIndex, juce::Component& near)
{
    auto& chain = mProcessor.getChain();
    const bool split = chain.isStageSplit(stageIndex);

    juce::PopupMenu menu;

    if (split)
    {
        menu.addSectionHeader("Parallel stage");
        menu.addItem(1, "Merge back to one path (discards side B)");
        menu.addSeparator();
        menu.addItem(2, "Side A hard left / Side B hard right");
        menu.addItem(3, "Both sides centred");
    }
    else
    {
        menu.addItem(4, "Split into two paths (A / B)");
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&near),
                       [this, stageIndex](int choice) {
                           auto& target = mProcessor.getChain();

                           switch (choice)
                           {
                               case 1: mProcessor.mergeStage(stageIndex); break;
                               case 2:
                                   target.setRowPan(stageIndex, 0, -1.0f);
                                   target.setRowPan(stageIndex, 1, 1.0f);
                                   break;
                               case 3:
                                   target.setRowPan(stageIndex, 0, 0.0f);
                                   target.setRowPan(stageIndex, 1, 0.0f);
                                   break;
                               case 4: mProcessor.splitStage(stageIndex); break;
                               default: return;
                           }

                           refresh();
                       });
}

void LaneView::showBlockMenu(const juce::String& uid)
{
    auto* block = mProcessor.getChain().getBlockByUid(uid);
    if (block == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem(1, block->isBypassed() ? "Enable" : "Bypass");
    menu.addItem(2, "Open editor");
    menu.addSeparator();
    menu.addItem(3, "Add block before");
    menu.addItem(4, "Add block after");
    menu.addSeparator();
    menu.addItem(5, "Remove");
    menu.addSeparator();

    // Per-block cost and latency, the numbers people actually want when a rig
    // starts struggling.
    const auto& load = block->getLoad();
    menu.addSectionHeader("CPU " + juce::String(load.getAverage() * 100.0f, 2) + "%  (peak "
                          + juce::String(load.getPeak() * 100.0f, 2) + "%)   Latency "
                          + juce::String(block->getLatencySamples()) + " smp");

    const auto found = mProcessor.getChain().findBlock(uid);
    const auto position = found.value_or(BlockPosition{});
    const bool split = mProcessor.getChain().isStageSplit(position.stage);

    menu.addSeparator();
    if (split)
        menu.addItem(6, "Merge this stage back to one path");
    else
        menu.addItem(7, "Split this stage into A / B");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, uid, position](int choice) {
        auto* target = mProcessor.getChain().getBlockByUid(uid);

        switch (choice)
        {
            case 1:
                if (target != nullptr)
                    target->setBypassed(!target->isBypassed());
                refresh();
                break;
            case 2:
                if (target != nullptr)
                    if (auto* plugin = target->getPlugin())
                    {
                        mEditorWindows.show(uid, *plugin);
                        refresh();
                    }
                break;
            case 3:
                if (auto* tile = mTiles.empty() ? nullptr : mTiles.front().tile.get())
                    addBlockAt(position, *tile);
                break;
            case 4:
                if (auto* tile = mTiles.empty() ? nullptr : mTiles.front().tile.get())
                    addBlockAt(BlockPosition{position.stage, position.row, position.index + 1}, *tile);
                break;
            case 5:
                mEditorWindows.close(uid);
                mProcessor.removeBlock(uid);
                if (mSelectedUid == uid)
                    selectBlock({});
                refresh();
                break;
            case 6: mProcessor.mergeStage(position.stage); refresh(); break;
            case 7: mProcessor.splitStage(position.stage); refresh(); break;
            default: break;
        }
    });
}

void LaneView::timerCallback()
{
    // Cheap live feedback: each tile's activity plus the I/O meters.
    for (auto& placed : mTiles)
    {
        if (auto* block = mProcessor.getChain().getBlockByUid(placed.tile->getUid()))
        {
            placed.tile->setLoad(block->getLoad().getAverage());
            placed.tile->setActivity(juce::jlimit(0.0f, 1.0f, block->getOutputLevel()));
            placed.tile->setBypassed(block->isBypassed());
            placed.tile->setEditorOpen(mEditorWindows.isOpen(placed.tile->getUid()));
        }
    }

    mInputBlock.setLevel(mProcessor.getInputLevel());
    mOutputBlock.setLevel(mProcessor.getOutputLevel());
}

void LaneView::paint(juce::Graphics& g)
{
    g.setColour(theme::colours::background);
    g.fillRect(getLocalBounds());
}

void LaneContent::paint(juce::Graphics& g)
{
    // Connectors between the blocks, so the lane reads as a signal path rather
    // than a row of unrelated boxes.
    g.setColour(theme::colours::outlineStrong);

    for (const auto& segment : mConnectors)
    {
        const auto y = static_cast<float>(segment.getCentreY());
        g.fillRect(juce::Rectangle<float>(static_cast<float>(segment.getX()), y - 0.75f,
                                          static_cast<float>(segment.getWidth()), 1.5f));

        // Arrow head.
        juce::Path head;
        const auto tip = static_cast<float>(segment.getRight());
        head.startNewSubPath(tip - 5.0f, y - 3.5f);
        head.lineTo(tip, y);
        head.lineTo(tip - 5.0f, y + 3.5f);
        g.strokePath(head, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }

    // Where a dragged tile would land: only as tall as the row it would join, so
    // on a split it is clear which side is being targeted.
    if (mDropIndicatorX >= 0)
    {
        const auto bandHeight = static_cast<float>(getHeight()) / static_cast<float>(mDropTotalRows);
        const auto top = bandHeight * static_cast<float>(mDropRow);

        g.setColour(theme::colours::accent);
        g.fillRoundedRectangle(static_cast<float>(mDropIndicatorX) - 1.5f, top + 8.0f, 3.0f,
                               juce::jmax(10.0f, bandHeight - 16.0f), 1.5f);
    }
}

void LaneView::resized()
{
    mViewport.setBounds(getLocalBounds());

    auto& chain = mProcessor.getChain();

    // How tall is the lane? A split stage needs two rows, so the whole strip
    // grows to the tallest stage.
    int maxRows = 1;
    for (int stageIndex = 0; stageIndex < chain.getNumStages(); ++stageIndex)
        maxRows = juce::jmax(maxRows, chain.getNumRows(stageIndex));

    const int singleRow = rowHeight();
    const int laneHeight = maxRows * singleRow;
    const int laneTop = juce::jmax(0, (getHeight() - laneHeight) / 2);

    // The ends sit centred against the whole (possibly two-row) lane.
    int x = 0;
    mInputBlock.setBounds(x, laneTop + (laneHeight - singleRow) / 2, theme::metrics::endBlockWidth, singleRow);
    x += theme::metrics::endBlockWidth + theme::metrics::arrowWidth;

    mStageGeometry.clear();
    size_t tileCursor = 0;
    size_t addCursor = 0;
    const int slotWidth = theme::metrics::blockWidth + theme::metrics::arrowWidth;

    for (int stageIndex = 0; stageIndex < chain.getNumStages(); ++stageIndex)
    {
        const int rows = chain.getNumRows(stageIndex);

        // Stage width is the widest of its rows, so both rows share a column.
        int widestRow = 0;
        for (int rowIndex = 0; rowIndex < rows; ++rowIndex)
        {
            const int count = static_cast<int>(chain.getBlocksInRow(stageIndex, rowIndex).size());
            widestRow = juce::jmax(widestRow, count * slotWidth + 32); // room for the row's "+"
        }

        StageGeometry geometry;
        geometry.x = x;
        geometry.width = juce::jmax(slotWidth, widestRow);
        geometry.rows = rows;
        mStageGeometry.push_back(geometry);

        // Rows stack vertically: side A on top, side B beneath it. Horizontal
        // would read as "one after the other", which is the opposite of what a
        // parallel split means.
        const int stageTop = laneTop + (laneHeight - rows * singleRow) / 2;

        for (int rowIndex = 0; rowIndex < rows; ++rowIndex)
        {
            const int rowY = stageTop + rowIndex * singleRow;
            int rowX = x;

            const int count = static_cast<int>(chain.getBlocksInRow(stageIndex, rowIndex).size());

            for (int i = 0; i < count && tileCursor < mTiles.size(); ++i, ++tileCursor)
            {
                mTiles[tileCursor].tile->setBounds(rowX, rowY + 3, theme::metrics::blockWidth,
                                                   singleRow - 6);
                rowX += slotWidth;
            }

            if (addCursor < mRowAddButtons.size())
            {
                mRowAddButtons[addCursor]->setBounds(rowX, rowY + singleRow / 2 - 13, 26, 26);
                ++addCursor;
            }
        }

        x += geometry.width;
    }

    // Trailing "+" appends a whole new stage at the end of the chain.
    mAddButton.setBounds(x, laneTop + laneHeight / 2 - 14, 28, 28);
    x += 28 + theme::metrics::arrowWidth;

    mOutputBlock.setBounds(x, laneTop + (laneHeight - singleRow) / 2, theme::metrics::endBlockWidth, singleRow);
    x += theme::metrics::endBlockWidth;

    mLaneContent.setSize(juce::jmax(x + theme::metrics::gap, getWidth()),
                         juce::jmax(getHeight(), laneHeight + 2 * theme::metrics::gap));

    // Connectors: one run into each stage and out of it. A split fans out to both
    // rows and back in, which is what makes the parallelism legible.
    std::vector<juce::Rectangle<int>> connectors;
    int previousRight = mInputBlock.getRight();
    int previousCentreY = mInputBlock.getBounds().getCentreY();

    for (int stageIndex = 0; stageIndex < static_cast<int>(mStageGeometry.size()); ++stageIndex)
    {
        const auto& geometry = mStageGeometry[static_cast<size_t>(stageIndex)];
        const int stageTop = laneTop + (laneHeight - geometry.rows * singleRow) / 2;

        for (int rowIndex = 0; rowIndex < geometry.rows; ++rowIndex)
        {
            const int rowCentreY = stageTop + rowIndex * singleRow + singleRow / 2;
            connectors.push_back({previousRight, juce::jmin(previousCentreY, rowCentreY),
                                  geometry.x - previousRight,
                                  std::abs(rowCentreY - previousCentreY) + 2});
        }

        previousRight = geometry.x + geometry.width;
        previousCentreY = laneTop + laneHeight / 2;
    }

    connectors.push_back({previousRight, previousCentreY - 1,
                          juce::jmax(0, mOutputBlock.getX() - previousRight), 2});

    mLaneContent.setConnectors(std::move(connectors));
}

} // namespace blockrig
