#include "ui/BlockWindow.h"

#include "ui/Theme.h"

namespace blockrig
{

BlockWindow::BlockWindow(juce::String title, juce::String subtitle, BlockCategory category,
                         std::unique_ptr<juce::Component> content)
    : mTitle(std::move(title))
    , mSubtitle(std::move(subtitle))
    , mCategory(category)
    , mContent(std::move(content))
{
    if (mContent != nullptr)
        addAndMakeVisible(*mContent);

    mClose.setTooltip("Close this window");
    mClose.onClick = [this] {
        if (onClose)
            onClose();
    };
    addAndMakeVisible(mClose);

    mPin.setTooltip("Pin this window to the canvas so it stays open while you work on other blocks");
    mPin.onClick = [this] {
        if (onTogglePin)
            onTogglePin();
    };
    addAndMakeVisible(mPin);

    // Keep at least the title bar reachable however far it is dragged.
    mConstrainer.setMinimumOnscreenAmounts(kTitleBarHeight, 60, 20, 60);

    setSize(kDefaultWidth, kDefaultHeight);
}

void BlockWindow::setPinned(bool shouldBePinned)
{
    mPinned = shouldBePinned;
    mPin.setButtonText(mPinned ? "Unpin" : "Pin");
    repaint();
}

juce::Rectangle<int> BlockWindow::getTitleBarBounds() const
{
    return getLocalBounds().removeFromTop(kTitleBarHeight);
}

void BlockWindow::mouseDown(const juce::MouseEvent& event)
{
    // Only the title bar drags; dragging from the body would fight the controls.
    if (getTitleBarBounds().contains(event.getPosition()))
        mDragger.startDraggingComponent(this, event);

    toFront(true);
}

void BlockWindow::mouseDrag(const juce::MouseEvent& event)
{
    if (getTitleBarBounds().contains(event.getMouseDownPosition()))
        mDragger.dragComponent(this, event, &mConstrainer);
}

void BlockWindow::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto accent = getCategoryColour(mCategory);

    // A soft drop shadow, so a floating window reads as above the rig rather
    // than pasted into it.
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(bounds.translated(0.0f, 3.0f).expanded(2.0f), theme::metrics::cornerRadius + 2.0f);

    g.setColour(theme::colours::panel);
    g.fillRoundedRectangle(bounds, theme::metrics::cornerRadius);

    g.setColour(mPinned ? accent.withAlpha(0.7f) : theme::colours::outlineStrong);
    g.drawRoundedRectangle(bounds, theme::metrics::cornerRadius, mPinned ? 1.8f : 1.2f);

    // Title bar, tinted by the block's category.
    auto titleBar = bounds.removeFromTop(static_cast<float>(kTitleBarHeight));
    g.setColour(accent.withAlpha(0.16f));
    g.fillRoundedRectangle(titleBar.withHeight(titleBar.getHeight() + 8.0f),
                           theme::metrics::cornerRadius);
    g.setColour(theme::colours::outline);
    g.fillRect(titleBar.getX(), titleBar.getBottom(), titleBar.getWidth(), 1.0f);

    auto text = titleBar.reduced(9.0f, 0.0f);
    text.removeFromRight(84.0f); // room for the buttons

    drawCategoryIcon(g, text.removeFromLeft(18.0f).reduced(1.0f, 6.0f), mCategory, accent, 1.3f);
    text.removeFromLeft(6.0f);

    g.setColour(theme::colours::text);
    g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    const auto titleWidth = juce::jmin(text.getWidth(), 190.0f);
    g.drawText(mTitle, text.removeFromLeft(titleWidth), juce::Justification::centredLeft, true);

    if (mSubtitle.isNotEmpty())
    {
        g.setColour(theme::colours::textFaint);
        g.setFont(juce::FontOptions(10.5f));
        g.drawText(mSubtitle, text, juce::Justification::centredLeft, true);
    }
}

void BlockWindow::resized()
{
    auto titleBar = getTitleBarBounds().reduced(5, 4);
    mClose.setBounds(titleBar.removeFromRight(26));
    titleBar.removeFromRight(4);
    mPin.setBounds(titleBar.removeFromRight(48));

    if (mContent != nullptr)
        mContent->setBounds(getLocalBounds().withTrimmedTop(kTitleBarHeight));
}

} // namespace blockrig
