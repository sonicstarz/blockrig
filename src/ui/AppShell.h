#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "BlockRigProcessor.h"
#include "ui/MainView.h"

namespace blockrig
{

/// The app's three screens: boot, home, rig.
///
/// Boot shows the wordmark immediately and sweeps the plug-in folders for
/// anything new or gone — a full first-time scan takes minutes, a sweep of an
/// unchanged machine takes seconds, and both belong behind the logo rather than
/// behind a menu item nobody presses. Home is where rigs are chosen and managed.
/// The rig screen is MainView, unchanged.
class AppShell final : public juce::Component
                     , private juce::Timer
{
public:
    AppShell(BlockRigProcessor& processor, juce::AudioDeviceManager* deviceManager);
    ~AppShell() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /// Boot begins when the shell first appears; calls this to start the sweep.
    void beginBoot();

    /// The quit path asks the rig screen about unsaved changes first.
    void requestQuit(std::function<void()> quitNow);

private:
    class HomeScreen;

    enum class Screen
    {
        boot,
        home,
        rig
    };

    void timerCallback() override;
    void showHome();
    void openRig(const juce::File& file);
    void openSetlist(const juce::File& setlistFile);
    void scanThreadBody();

    BlockRigProcessor& mProcessor;
    juce::AudioDeviceManager* mDeviceManager;

    Screen mScreen = Screen::boot;

    std::unique_ptr<HomeScreen> mHome;
    std::unique_ptr<MainView> mMainView;

    // Boot state, written by the scan thread and read by paint().
    std::thread mScanThread;
    std::atomic<bool> mScanDone{false};
    juce::CriticalSection mProgressLock;
    juce::String mProgressText;
    int mProgressScanned = 0;
    int mProgressTotal = 0;
    int mBootElapsedTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppShell)
};

} // namespace blockrig
