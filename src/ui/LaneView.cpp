#include "ui/LaneView.h"

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
    if (std::abs(mActivity - level) < 0.01f)
        return;
    mActivity = level;
    repaint();
}

void BlockTile::setLoad(float fractionOfBudget)
{
    mLoad = fractionOfBudget;
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

    content.removeFromTop(14.0f);

    g.setColour(mBypassed ? theme::colours::textFaint : theme::colours::text);
    g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    g.drawFittedText(mName, content.removeFromTop(30.0f).toNearestInt(), juce::Justification::topLeft, 2, 0.85f);

    g.setColour(theme::colours::textFaint);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText(mSubtitle, content.removeFromTop(13.0f), juce::Justification::topLeft, true);

    // Activity strip at the bottom.
    theme::drawLevelMeter(g, content.removeFromBottom(4.0f), mBypassed ? 0.0f : mActivity);

    if (mDragging)
    {
        g.setColour(theme::colours::accent.withAlpha(0.12f));
        g.fillRoundedRectangle(bounds, theme::metrics::cornerRadius);
    }
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

    mDragging = true;
    repaint();

    if (onDragToIndex)
    {
        // Report in lane coordinates, since the parent owns the ordering.
        const auto inParent = event.getEventRelativeTo(getParentComponent());
        onDragToIndex(inParent.x, false);
    }
}

void BlockTile::mouseUp(const juce::MouseEvent& event)
{
    if (!mDragging)
        return;

    mDragging = false;
    repaint();

    if (onDragToIndex)
    {
        const auto inParent = event.getEventRelativeTo(getParentComponent());
        onDragToIndex(inParent.x, true);
    }
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
    if (std::abs(mLevel - level) < 0.01f)
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

    mAddButton.onClick = [this] { addBlockAt(static_cast<int>(mTiles.size())); };
    mAddButton.setTooltip("Add a block to the end of the chain");

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

    for (auto* block : mProcessor.getChain().getBlocks())
    {
        auto* plugin = block->getPlugin();

        juce::String subtitle;
        if (plugin != nullptr)
        {
            juce::PluginDescription description;
            plugin->fillInPluginDescription(description);
            subtitle = description.manufacturerName;

            const auto latency = block->getLatencySamples();
            if (latency > 0)
                subtitle += "  " + juce::String(latency) + " smp";
        }

        auto tile = std::make_unique<BlockTile>(block->getUid(), shortenName(block->getDisplayName()), subtitle);
        const auto uid = block->getUid();

        tile->setBypassed(block->isBypassed());
        tile->setEditorOpen(mEditorWindows.isOpen(uid));
        tile->setSelected(uid == mSelectedUid);

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
                if (auto* plugin = target->getPlugin())
                {
                    mEditorWindows.show(uid, *plugin);
                    refresh();
                }
        };

        tile->onShowMenu = [this, uid] { showBlockMenu(uid); };

        tile->onDragToIndex = [this, uid](int xInLane, bool dropped) {
            const int target = indexForX(xInLane);

            if (!dropped)
            {
                if (mDropIndicatorIndex != target)
                {
                    mDropIndicatorIndex = target;
                    mLaneContent.repaint();
                }
                return;
            }

            mDropIndicatorIndex = -1;
            mProcessor.moveBlock(uid, target);
            refresh();
        };

        mLaneContent.addAndMakeVisible(*tile);
        mTiles.push_back(std::move(tile));
    }

    resized();
    mLaneContent.repaint();
}

int LaneView::indexForX(int x) const
{
    // Each slot is a tile plus the arrow gap after it; the input block offsets
    // everything by its own width.
    const int slotWidth = theme::metrics::blockWidth + theme::metrics::arrowWidth;
    const int laneStart = theme::metrics::endBlockWidth + theme::metrics::arrowWidth;
    const int index = (x - laneStart + slotWidth / 2) / slotWidth;
    return juce::jlimit(0, static_cast<int>(mTiles.size()), index);
}

void LaneView::selectBlock(const juce::String& uid)
{
    if (mSelectedUid == uid)
        return;

    mSelectedUid = uid;

    for (auto& tile : mTiles)
        tile->setSelected(tile->getUid() == uid);

    mInputBlock.setSelected(false);
    mOutputBlock.setSelected(false);

    if (onSelectionChanged)
        onSelectionChanged();
}

void LaneView::addBlockAt(int index)
{
    BlockPicker::show(mProcessor.getCatalog(), mAddButton,
                      [this, index](const juce::PluginDescription& description) {
                          mProcessor.addBlock(description, index, [this](juce::String uid, juce::String error) {
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

    int index = 0;
    for (size_t i = 0; i < mTiles.size(); ++i)
        if (mTiles[i]->getUid() == uid)
            index = static_cast<int>(i);

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, uid, index](int choice) {
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
            case 3: addBlockAt(index); break;
            case 4: addBlockAt(index + 1); break;
            case 5:
                mEditorWindows.close(uid);
                mProcessor.removeBlock(uid);
                if (mSelectedUid == uid)
                    selectBlock({});
                refresh();
                break;
            default: break;
        }
    });
}

void LaneView::timerCallback()
{
    // Cheap live feedback: each tile's activity plus the I/O meters.
    for (auto& tile : mTiles)
    {
        if (auto* block = mProcessor.getChain().getBlockByUid(tile->getUid()))
        {
            tile->setLoad(block->getLoad().getAverage());
            tile->setActivity(juce::jlimit(0.0f, 1.0f, block->getLoad().getAverage() * 8.0f));
            tile->setBypassed(block->isBypassed());
            tile->setEditorOpen(mEditorWindows.isOpen(tile->getUid()));
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

void LaneView::resized()
{
    mViewport.setBounds(getLocalBounds());

    const int height = theme::metrics::blockHeight;
    const int y = juce::jmax(0, (getHeight() - height) / 2);

    int x = 0;
    mInputBlock.setBounds(x, y, theme::metrics::endBlockWidth, height);
    x += theme::metrics::endBlockWidth + theme::metrics::arrowWidth;

    for (auto& tile : mTiles)
    {
        tile->setBounds(x, y, theme::metrics::blockWidth, height);
        x += theme::metrics::blockWidth + theme::metrics::arrowWidth;
    }

    mAddButton.setBounds(x, y + height / 2 - 14, 28, 28);
    x += 28 + theme::metrics::arrowWidth;

    mOutputBlock.setBounds(x, y, theme::metrics::endBlockWidth, height);
    x += theme::metrics::endBlockWidth;

    mLaneContent.setSize(juce::jmax(x + theme::metrics::gap, getWidth()), getHeight());
}

} // namespace blockrig
