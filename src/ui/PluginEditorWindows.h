#pragma once

#include <functional>
#include <map>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace blockrig
{

/// Manages the floating windows that hold third-party plug-in editors.
///
/// Every host in this space uses floating windows, and every one of them gets
/// complaints about window sprawl. The mitigations here are the set Blue Cat
/// PatchWork settled on, which is the state of the art: remember each window's
/// position per block, raise instead of reopening, Escape closes the front one,
/// close-all in one action, and an optional always-on-top.
class PluginEditorWindows
{
public:
    // Both declared out-of-line and defined in the .cpp: Window is an incomplete
    // type here, and an implicitly generated special member would be instantiated
    // wherever this class is used, where it cannot see Window's size.
    PluginEditorWindows();
    ~PluginEditorWindows();

    /// Shows (or raises) the editor for this block. Falls back to a generic
    /// parameter list when the plug-in has no editor of its own.
    void show(const juce::String& blockUid, juce::AudioPluginInstance& plugin);

    void close(const juce::String& blockUid);
    void closeAll();

    /// Closes the frontmost editor; bound to Escape.
    void closeFrontmost();

    bool isOpen(const juce::String& blockUid) const;

    void setAlwaysOnTop(bool shouldBeOnTop);
    bool getAlwaysOnTop() const { return mAlwaysOnTop; }

    /// Position memory, so a rig reopens its editors where the user left them.
    struct WindowBounds
    {
        int x = 0, y = 0, width = 0, height = 0;
        bool valid = false;
    };

    WindowBounds getRememberedBounds(const juce::String& blockUid) const;
    void rememberBounds(const juce::String& blockUid, WindowBounds bounds);

    /// Called when a window is closed by its own title bar, so the lane can
    /// update the block's "editor open" indicator.
    std::function<void(juce::String blockUid)> onWindowClosed;

private:
    class Window;

    std::map<juce::String, std::unique_ptr<Window>> mWindows;
    std::map<juce::String, WindowBounds> mRemembered;
    juce::StringArray mOrder; ///< most recently shown last, for closeFrontmost
    bool mAlwaysOnTop = false;
};

} // namespace blockrig
