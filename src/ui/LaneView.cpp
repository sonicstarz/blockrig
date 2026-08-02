#include "ui/LaneView.h"

#include "blocks/nam/NamBlockProcessor.h"
#include "ui/BlockPicker.h"
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
    setTooltip(mName + (mSubtitle.isNotEmpty() ? juce::String::fromUTF8(" — ") + mSubtitle : juce::String()));
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

void BlockTile::setCategory(BlockCategory category)
{
    if (mCategory == category)
        return;
    mCategory = category;
    repaint();
}

void BlockTile::setMissing(bool isMissing)
{
    if (mMissing == isMissing)
        return;

    mMissing = isMissing;
    repaint();
}

void BlockTile::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    auto square = bounds.removeFromTop(theme::metrics::blockSquare).toFloat().reduced(2.0f);
    auto labelArea = bounds;

    const auto categoryColour = mMissing ? theme::colours::bad : getCategoryColour(mCategory);
    const auto tint = categoryColour;

    // 4c chip: category fill at 10-12%, 2px category border, glow when open.
    // A bypassed chip drops to 45% opacity wholesale rather than desaturating.
    if (mBypassed)
        g.beginTransparencyLayer(0.45f);

    if (mEditorOpen)
    {
        g.setColour(categoryColour.withAlpha(0.28f));
        g.fillRoundedRectangle(square.expanded(3.0f), theme::metrics::cornerRadius + 2.0f);
    }

    g.setColour(categoryColour.withAlpha(0.11f));
    g.fillRoundedRectangle(square, theme::metrics::cornerRadius);

    // Engaged wears a thick coloured outline; bypassed loses it entirely. The
    // outline IS the on-light - engaged versus bypassed has to read from across
    // the room, not from a strikethrough you lean in for.
    if (mMissing)
    {
        juce::Path outline;
        outline.addRoundedRectangle(square, theme::metrics::cornerRadius);
        const float dashes[] = {5.0f, 4.0f};
        juce::PathStrokeType(2.0f).createDashedStroke(outline, outline, dashes, 2);
        g.setColour(theme::colours::bad.withAlpha(mSelected ? 1.0f : 0.75f));
        g.fillPath(outline);
    }
    else
    {
        g.setColour(mSelected ? theme::colours::text.withAlpha(0.9f) : tint);
        g.drawRoundedRectangle(square, theme::metrics::cornerRadius, mSelected ? 2.5f : 2.0f);
    }

    if (mMissing)
    {
        // An exclamation mark, not a category glyph: this slot holds a place,
        // it does not do anything.
        auto glyph = square.reduced(15.0f).withTrimmedBottom(6.0f);
        g.setColour(theme::colours::bad);
        const auto centreX = glyph.getCentreX();
        g.fillRoundedRectangle(centreX - 1.6f, glyph.getY() + 2.0f, 3.2f,
                               glyph.getHeight() * 0.55f, 1.6f);
        g.fillEllipse(centreX - 2.0f, glyph.getBottom() - 5.0f, 4.0f, 4.0f);
    }
    else
    {
        // Line-art glyph for the category: a rig should be readable by shape
        // before any of the names are read.
        drawCategoryIcon(g, square.reduced(15.0f).withTrimmedBottom(6.0f), mCategory,
                         mBypassed ? theme::colours::textFaint : tint, 1.7f);
    }

    // Level along the bottom edge of the square.
    theme::drawLevelMeter(g, square.reduced(9.0f, 0.0f).removeFromBottom(4.0f).translated(0.0f, -5.0f),
                          mBypassed ? 0.0f : mActivity);

    // Side marker for a split path, and a dot when an editor window is open.
    if (mRowLabel.isNotEmpty())
    {
        g.setColour(theme::colours::accent);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(mRowLabel, square.reduced(5.0f).removeFromTop(12.0f), juce::Justification::topLeft, false);
    }

    if (mEditorOpen)
    {
        g.setColour(theme::colours::accent.withAlpha(0.4f));
        g.fillEllipse(square.getRight() - 15.0f, square.getY() + 2.0f, 12.0f, 12.0f);
        g.setColour(theme::colours::accent);
        g.fillEllipse(square.getRight() - 12.5f, square.getY() + 4.5f, 7.0f, 7.0f);
    }

    if (mBypassed)
        g.endTransparencyLayer();

    g.setColour(mSelected ? theme::colours::text : theme::colours::textDim);
    g.setFont(theme::fonts::ui(12.0f, 500));
    g.drawFittedText(mName + (mBypassed ? juce::String::fromUTF8("  \xc2\xb7 byp") : juce::String()),
                     labelArea.reduced(1, 1), juce::Justification::centredTop, 2, 0.8f);
}

void BlockTile::mouseDown(const juce::MouseEvent& event)
{
    if (onSelect)
        onSelect();

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
    // Click-and-release opens the block; click-and-hold-and-move drags it.
    // Opening on mouseDown made every drag start by throwing a window in
    // your face.
    if (!mDragging)
    {
        if (!event.mods.isPopupMenu() && onOpenEditor)
            onOpenEditor();
        return;
    }

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
    auto bounds = getLocalBounds();
    auto square = bounds.removeFromTop(theme::metrics::blockSquare).toFloat().reduced(2.0f);

    g.setColour(theme::colours::panel);
    g.fillRoundedRectangle(square, theme::metrics::cornerRadius);
    g.setColour(mSelected ? theme::colours::accent : theme::colours::outlineStrong);
    g.drawRoundedRectangle(square, theme::metrics::cornerRadius, mSelected ? 2.4f : 1.4f);

    g.setColour(theme::colours::text);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(mKind == Kind::input ? "IN" : "OUT", square.withTrimmedBottom(12.0f),
               juce::Justification::centred, false);

    theme::drawLevelMeter(g, square.reduced(9.0f, 0.0f).removeFromBottom(4.0f).translated(0.0f, -5.0f),
                          mLevel);

    g.setColour(theme::colours::textFaint);
    g.setFont(juce::FontOptions(11.0f));
    g.drawFittedText(mCaption, bounds.reduced(1, 1), juce::Justification::centredTop, 2, 0.8f);
}

void EndBlock::mouseDown(const juce::MouseEvent&)
{
    if (onSelect)
        onSelect();
}

//==============================================================================
LaneView::LaneView(BlockRigProcessor& processor)
    : mProcessor(processor)
{
    mViewport.setViewedComponent(&mLaneContent, false);
    mViewport.setScrollBarsShown(false, true, false, true);
    addAndMakeVisible(mViewport);

    mLaneContent.addAndMakeVisible(mInputBlock);
    mLaneContent.addAndMakeVisible(mOutputBlock);
    mLaneContent.addAndMakeVisible(mAddButton);
    mLaneContent.addAndMakeVisible(mAddFirstButton);

    mAddFirstButton.setTooltip("Add a block at the start of the chain");
    mAddFirstButton.onClick = [this] {
        // A new first stage, so it lands in front of a split rather than on one
        // of its sides.
        addBlockAt(BlockPosition{0, 0, 0, true}, mAddFirstButton);
    };

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


    refresh();
    startTimerHz(15);
}

LaneView::~LaneView()
{
    stopTimer();
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
                tile->setEditorOpen(isBlockWindowOpen != nullptr && isBlockWindowOpen(uid));
                tile->setSelected(uid == mSelectedUid);
                tile->setRowLabel(rows > 1 ? (rowIndex == 0 ? "A" : "B") : juce::String());

                tile->setMissing(block->isMissing());

                if (plugin == nullptr && block->isMissing())
                {
                    tile->setTooltip(block->getMissingDescription().name
                                     + " is not installed. The block keeps its place and its settings; "
                                       "reinstall the plug-in and rescan to bring it back.");
                }

                if (plugin != nullptr)
                {
                    juce::PluginDescription description;
                    plugin->fillInPluginDescription(description);
                    tile->setCategory(categoriseBlock(description));
                }

                tile->onSelect = [this, uid] { selectBlock(uid); };

                tile->onToggleBypass = [this, uid] {
                    if (auto* target = mProcessor.getChain().getBlockByUid(uid))
                    {
                        target->setBypassed(!target->isBypassed());
                        refresh();
                    }
                };

                // Single click already opens the block's window; a double click
                // must not open a second, different kind of window on top of it.
                tile->onOpenEditor = [this, uid] {
                    if (onBlockActivated)
                        onBlockActivated(uid);
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
    // Favourites first: a saved block is a plug-in you already chose once, so
    // reaching it should be shorter than finding it again in a list of 850.
    const auto favorites = mProcessor.getFavorites().getEntries();

    if (!favorites.isEmpty())
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Browse all blocks...");
        menu.addSectionHeader("Favourites");

        int id = 100;
        for (const auto& entry : favorites)
            menu.addItem(id++, entry.name + "   (" + entry.description.name + ")");

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&near),
                           [this, position, favorites, &near](int choice) {
            if (choice == 1)
            {
                showPickerAt(position, near);
            }
            else if (choice >= 100 && choice - 100 < favorites.size())
            {
                const auto& entry = favorites[choice - 100];
                mProcessor.addBlockWithState(entry.description, position,
                                             mProcessor.getFavorites().loadState(entry.file));
            }
        });
        return;
    }

    showPickerAt(position, near);
}

void LaneView::showPickerAt(BlockPosition position, juce::Component& near)
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

void LaneView::saveFavorite(const juce::String& uid)
{
    auto* block = mProcessor.getChain().getBlockByUid(uid);
    if (block == nullptr)
        return;

    auto window = std::make_shared<juce::AlertWindow>("Save as favourite", "",
                                                      juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", block->getDisplayName());
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    window->enterModalState(true, juce::ModalCallbackFunction::create([this, uid, window](int result) {
        if (result != 1)
            return;

        const auto name = window->getTextEditorContents("name").trim();
        if (name.isEmpty())
            return;

        if (auto* target = mProcessor.getChain().getBlockByUid(uid))
            mProcessor.getFavorites().save(*target, name);
    }));
}

void LaneView::showBlockMenu(const juce::String& uid)
{
    auto* block = mProcessor.getChain().getBlockByUid(uid);
    if (block == nullptr)
        return;

    const auto found = mProcessor.getChain().findBlock(uid);
    const auto position = found.value_or(BlockPosition{});
    const bool split = mProcessor.getChain().isStageSplit(position.stage);

    // 4e's order: act on the block, then change the chain around it, then the
    // destructive item, then the block's numbers as a footer.
    juce::PopupMenu menu;
    menu.addItem(2, "Open editor");
    menu.addItem(1, block->isBypassed() ? "Enable" : "Bypass");
    menu.addSeparator();
    menu.addItem(3, "Add block before");
    menu.addItem(4, "Add block after");
    menu.addItem(11, "Replace...");
    if (split)
        menu.addItem(6, "Merge this stage back to one path");
    else
        menu.addItem(7, "Split this stage into A / B");
    menu.addSeparator();
    menu.addItem(8, "Copy", !block->isMissing());
    menu.addItem(9, "Paste after", mProcessor.getClipboard().hasContent());
    menu.addItem(10, "Save as favourite...", !block->isMissing());
    menu.addSeparator();
    menu.addItem(juce::PopupMenu::Item("Remove").setID(5).setColour(theme::colours::bad));
    menu.addSeparator();

    // Per-block cost and latency, the numbers people actually want when a rig
    // starts struggling.
    const auto& load = block->getLoad();
    menu.addItem(juce::PopupMenu::Item("CPU " + juce::String(load.getAverage() * 100.0f, 2)
                                       + juce::String::fromUTF8("% \xc2\xb7 peak ")
                                       + juce::String(load.getPeak() * 100.0f, 2)
                                       + juce::String::fromUTF8("% \xc2\xb7 ")
                                       + juce::String(block->getLatencySamples()) + " smp")
                     .setEnabled(false));

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, uid, position](int choice) {
        auto* target = mProcessor.getChain().getBlockByUid(uid);

        switch (choice)
        {
            case 8:
                if (target != nullptr)
                    mProcessor.getClipboard().copy(*target);
                break;
            case 9:
            {
                auto& clipboard = mProcessor.getClipboard();
                if (clipboard.hasContent())
                {
                    mProcessor.addBlockWithState(
                        clipboard.getDescription(),
                        BlockPosition{position.stage, position.row, position.index + 1},
                        clipboard.getState(), clipboard.wasBypassed());
                }
                break;
            }
            case 10:
                if (target != nullptr)
                    saveFavorite(uid);
                break;
            case 1:
                if (target != nullptr)
                    target->setBypassed(!target->isBypassed());
                refresh();
                break;
            case 2:
                if (onBlockActivated)
                    onBlockActivated(uid);
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
                mProcessor.removeBlock(uid);
                if (mSelectedUid == uid)
                    selectBlock({});
                refresh();
                break;
            case 11:
            {
                // Replace: pick a block for this slot, then swap it in.
                juce::Component* anchor = this;
                for (auto& placed : mTiles)
                    if (placed.position.stage == position.stage && placed.position.row == position.row
                        && placed.position.index == position.index)
                        anchor = placed.tile.get();

                BlockPicker::show(mProcessor.getCatalog(), *anchor,
                                  [this, uid, position](const juce::PluginDescription& description) {
                                      mProcessor.removeBlock(uid);
                                      mProcessor.addBlock(description, position,
                                                          [this](juce::String newUid, juce::String) {
                                                              refresh();
                                                              selectBlock(newUid);
                                                          });
                                  });
                break;
            }
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
            placed.tile->setEditorOpen(isBlockWindowOpen != nullptr && isBlockWindowOpen(placed.tile->getUid()));
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
    for (const auto& connector : mConnectors)
    {
        const auto from = connector.from.toFloat();
        const auto to = connector.to.toFloat();

        juce::Path path;
        path.startNewSubPath(from);

        if (connector.branch && std::abs(to.y - from.y) > 2.0f)
        {
            // Elbow out, run along the row, elbow back — the shape that reads as
            // "this path leaves the chain and returns to it".
            const auto corner = juce::jmin(9.0f, std::abs(to.y - from.y) * 0.5f);
            const auto midX = from.x + juce::jmax(10.0f, (to.x - from.x) * 0.45f);
            const auto direction = to.y > from.y ? 1.0f : -1.0f;

            path.lineTo(midX - corner, from.y);
            path.quadraticTo(midX, from.y, midX, from.y + corner * direction);
            path.lineTo(midX, to.y - corner * direction);
            path.quadraticTo(midX, to.y, midX + corner, to.y);
            path.lineTo(to);
        }
        else
        {
            path.lineTo(to);
        }

        // Amber signal wires, fading toward the border colour at the ends —
        // the design's endcap fade, done as a horizontal gradient.
        juce::ColourGradient wire(theme::colours::outline, from.x, from.y,
                                  theme::colours::outline, to.x, to.y, false);
        wire.addColour(0.2, theme::colours::accent.withAlpha(0.85f));
        wire.addColour(0.8, theme::colours::accent.withAlpha(0.85f));
        g.setGradientFill(wire);
        g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

        // Dots where a path divides or rejoins, as in a signal diagram.
        if (connector.junction)
        {
            g.setColour(theme::colours::accent);
            g.fillEllipse(from.x - 2.5f, from.y - 2.5f, 5.0f, 5.0f);
        }
    }

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
    x += theme::metrics::endBlockWidth + theme::metrics::arrowWidth / 2;

    mAddFirstButton.setBounds(x, laneTop + laneHeight / 2 - 13, 26, 26);
    x += 26 + theme::metrics::arrowWidth / 2;

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

    // Routing. A single-row stage is a straight run; a split leaves the main line
    // at a junction, runs its rows, and rejoins at another junction so the signal
    // returns to one path in full stereo.
    std::vector<LaneContent::Connector> connectors;

    const int mainY = laneTop + laneHeight / 2;
    const int squareCentre = theme::metrics::blockSquare / 2;
    int previousRight = mAddFirstButton.getRight();

    connectors.push_back({{mInputBlock.getRight(), mainY}, {mAddFirstButton.getX(), mainY}, false, false});

    for (int stageIndex = 0; stageIndex < static_cast<int>(mStageGeometry.size()); ++stageIndex)
    {
        const auto& geometry = mStageGeometry[static_cast<size_t>(stageIndex)];
        const int stageTop = laneTop + (laneHeight - geometry.rows * singleRow) / 2;
        const bool split = geometry.rows > 1;

        for (int rowIndex = 0; rowIndex < geometry.rows; ++rowIndex)
        {
            const int rowY = stageTop + rowIndex * singleRow + squareCentre;
            connectors.push_back({{previousRight, mainY}, {geometry.x, rowY}, split, split && rowIndex == 0});
        }

        previousRight = geometry.x + geometry.width;

        if (split)
        {
            const int rejoinX = previousRight + theme::metrics::arrowWidth / 2;

            for (int rowIndex = 0; rowIndex < geometry.rows; ++rowIndex)
            {
                const int rowY = stageTop + rowIndex * singleRow + squareCentre;
                connectors.push_back({{previousRight, rowY}, {rejoinX, mainY}, true, false});
            }

            // The dot where the two paths become one again.
            connectors.push_back({{rejoinX, mainY}, {rejoinX, mainY}, false, true});
            previousRight = rejoinX;
        }
    }

    connectors.push_back({{previousRight, mainY}, {mOutputBlock.getX(), mainY}, false, false});

    mLaneContent.setConnectors(std::move(connectors));
}

} // namespace blockrig
