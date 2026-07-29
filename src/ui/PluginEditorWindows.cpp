#include "ui/PluginEditorWindows.h"

#include "ui/Theme.h"

namespace blockrig
{

/// One floating editor window. Owns whichever editor component the plug-in gave
/// us — its own, or JUCE's generic parameter view as a fallback.
class PluginEditorWindows::Window final : public juce::DocumentWindow
{
public:
    Window(PluginEditorWindows& owner, juce::String blockUid, juce::AudioPluginInstance& plugin)
        : juce::DocumentWindow(plugin.getName(), theme::colours::background, juce::DocumentWindow::closeButton)
        , mOwner(owner)
        , mUid(std::move(blockUid))
    {
        setUsingNativeTitleBar(true);

        if (auto* editor = plugin.hasEditor() ? plugin.createEditorIfNeeded() : nullptr)
        {
            setContentOwned(editor, true);
            // Plug-in editors size themselves; honour their resize behaviour.
            setResizable(editor->isResizable(), false);
        }
        else
        {
            // No editor of its own: JUCE's generic parameter list is a usable
            // fallback and better than an empty window.
            setContentOwned(new juce::GenericAudioProcessorEditor(plugin), true);
            setResizable(true, false);
            centreWithSize(420, 500);
        }

        const auto remembered = mOwner.getRememberedBounds(mUid);
        if (remembered.valid)
            setBounds(remembered.x, remembered.y, juce::jmax(120, remembered.width),
                      juce::jmax(80, remembered.height));
        else
            centreAroundComponent(nullptr, getWidth(), getHeight());

        setAlwaysOnTop(mOwner.getAlwaysOnTop());
        setVisible(true);
    }

    ~Window() override
    {
        // Remember where the user put it before it disappears.
        mOwner.rememberBounds(mUid, {getX(), getY(), getWidth(), getHeight(), true});
        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        // Deferred: this is called from the window's own event handling.
        const auto uid = mUid;
        auto* owner = &mOwner;
        juce::MessageManager::callAsync([owner, uid] {
            owner->close(uid);
            if (owner->onWindowClosed)
                owner->onWindowClosed(uid);
        });
    }

    const juce::String& getBlockUid() const { return mUid; }

private:
    PluginEditorWindows& mOwner;
    juce::String mUid;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Window)
};

PluginEditorWindows::PluginEditorWindows() = default;

PluginEditorWindows::~PluginEditorWindows()
{
    mWindows.clear();
}

void PluginEditorWindows::show(const juce::String& blockUid, juce::AudioPluginInstance& plugin)
{
    // Already open: raise rather than spawn a duplicate.
    if (const auto found = mWindows.find(blockUid); found != mWindows.end())
    {
        found->second->toFront(true);
        mOrder.removeString(blockUid);
        mOrder.add(blockUid);
        return;
    }

    mWindows[blockUid] = std::make_unique<Window>(*this, blockUid, plugin);
    mOrder.removeString(blockUid);
    mOrder.add(blockUid);
}

void PluginEditorWindows::close(const juce::String& blockUid)
{
    mWindows.erase(blockUid);
    mOrder.removeString(blockUid);
}

void PluginEditorWindows::closeAll()
{
    mWindows.clear();
    mOrder.clear();
}

void PluginEditorWindows::closeFrontmost()
{
    if (mOrder.isEmpty())
        return;

    const auto uid = mOrder[mOrder.size() - 1];
    close(uid);

    if (onWindowClosed)
        onWindowClosed(uid);
}

bool PluginEditorWindows::isOpen(const juce::String& blockUid) const
{
    return mWindows.find(blockUid) != mWindows.end();
}

void PluginEditorWindows::setAlwaysOnTop(bool shouldBeOnTop)
{
    mAlwaysOnTop = shouldBeOnTop;

    for (auto& [uid, window] : mWindows)
    {
        juce::ignoreUnused(uid);
        window->setAlwaysOnTop(shouldBeOnTop);
    }
}

PluginEditorWindows::WindowBounds PluginEditorWindows::getRememberedBounds(const juce::String& blockUid) const
{
    if (const auto found = mRemembered.find(blockUid); found != mRemembered.end())
        return found->second;
    return {};
}

void PluginEditorWindows::rememberBounds(const juce::String& blockUid, WindowBounds bounds)
{
    mRemembered[blockUid] = bounds;
}

} // namespace blockrig
