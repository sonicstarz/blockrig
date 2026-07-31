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
    // Only the title bar drags — and docked editors do not drag at all; the
    // dock is their place.
    if (!mDocked && getTitleBarBounds().contains(event.getPosition()))
        mDragger.startDraggingComponent(this, event);

    toFront(true);
}

void BlockWindow::mouseDrag(const juce::MouseEvent& event)
{
    if (!mDocked && getTitleBarBounds().contains(event.getMouseDownPosition()))
        mDragger.dragComponent(this, event, &mConstrainer);
}

void BlockWindow::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto accent = getCategoryColour(mCategory);
    const auto radius = theme::metrics::radiusXl;

    if (!mDocked)
    {
        // Floating panels sit above the rig; a shadow says so.
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRoundedRectangle(bounds.translated(0.0f, 8.0f).expanded(4.0f), radius + 2.0f);
    }

    // Glass: layered translucent fills approximate the backdrop blur the design
    // asks for, which JUCE cannot do.
    g.setColour(juce::Colour(0xff15131d).withAlpha(0.92f));
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(mPinned ? accent.withAlpha(0.6f) : theme::colours::outline);
    g.drawRoundedRectangle(bounds, radius, mPinned ? 1.6f : 1.0f);

    // Category header strip: 8% tint, 25% bottom border, and the solid bar.
    auto titleBar = bounds.removeFromTop(static_cast<float>(kTitleBarHeight));
    g.setColour(accent.withAlpha(0.08f));
    g.fillRoundedRectangle(titleBar, radius);
    g.setColour(accent.withAlpha(0.25f));
    g.fillRect(titleBar.getX(), titleBar.getBottom() - 1.0f, titleBar.getWidth(), 1.0f);

    auto text = titleBar.reduced(14.0f, 0.0f);
    text.removeFromRight(84.0f); // room for the buttons

    // The 8×26 category bar with glow.
    const juce::Rectangle<float> bar(text.getX(), titleBar.getCentreY() - 13.0f, 8.0f, 26.0f);
    g.setColour(accent.withAlpha(0.35f));
    g.fillRoundedRectangle(bar.expanded(2.5f), 5.0f);
    g.setColour(accent);
    g.fillRoundedRectangle(bar, 4.0f);
    text.removeFromLeft(18.0f);

    g.setColour(theme::colours::text);
    g.setFont(theme::fonts::ui(16.0f, 700));
    const auto titleWidth = juce::jmin(text.getWidth(), 220.0f);
    g.drawText(mTitle, text.removeFromLeft(titleWidth), juce::Justification::centredLeft, true);

    if (mSubtitle.isNotEmpty())
    {
        g.setColour(theme::colours::textFaint);
        g.setFont(theme::fonts::mono(11.0f));
        g.drawText(mSubtitle, text.withTrimmedLeft(8.0f), juce::Justification::centredLeft, true);
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
