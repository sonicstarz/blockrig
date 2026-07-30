#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/BlockCategories.h"

namespace blockrig
{

/// A draggable panel inside the app, not an OS window.
///
/// In-app so the backdrop can be dimmed behind it and so pinning one to the
/// canvas is just a reparent rather than a different kind of object. Opens at a
/// modest size rather than filling the app, because the point is to see it
/// against the rig rather than instead of it.
class BlockWindow final : public juce::Component
{
public:
    BlockWindow(juce::String title, juce::String subtitle, BlockCategory category,
                std::unique_ptr<juce::Component> content);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

    /// Identifies which block this belongs to, so it is not opened twice.
    juce::String blockUid;

    void setPinned(bool shouldBePinned);
    bool isPinned() const { return mPinned; }

    std::function<void()> onClose;
    std::function<void()> onTogglePin;

    static constexpr int kTitleBarHeight = 30;
    static constexpr int kDefaultWidth = 430;
    static constexpr int kDefaultHeight = 300;

private:
    juce::Rectangle<int> getTitleBarBounds() const;

    juce::String mTitle, mSubtitle;
    BlockCategory mCategory;
    std::unique_ptr<juce::Component> mContent;

    juce::TextButton mClose{"X"};
    juce::TextButton mPin{"Pin"};
    bool mPinned = false;

    juce::ComponentDragger mDragger;
    juce::ComponentBoundsConstrainer mConstrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockWindow)
};

} // namespace blockrig
