#pragma once

#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "BlockRigProcessor.h"
#include "ui/CpuMeter.h"
#include "ui/HeaderMeters.h"
#include "ui/TransportBar.h"
#include "state/RigFiles.h"
#include "ui/LaneView.h"
#include "ui/BlockWindow.h"
#include "ui/Theme.h"

namespace blockrig
{

/// The whole interface: header, lane, and the panel for whatever is selected.
///
/// Used by both deployments. `deviceManager` is null in the plug-in build, which
/// is what switches off the audio-settings affordances — inside a DAW the host
/// owns the device.
class MainView final : public juce::Component
                     , private juce::KeyListener
                     , private juce::Timer
{
public:
    MainView(BlockRigProcessor& processor, juce::AudioDeviceManager* deviceManager);
    ~MainView() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /// Called after a rig is restored from disk, so the lane and panels catch up.
    void rigWasRestored();

private:
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    void timerCallback() override;

    void showSettings();
    void startScan();
    void refreshHeader();
    void showRigMenu();
    void saveRig(bool forceChooser);
    void openRig();
    void reportRestore(const rigstate::RestoreResult& result, const juce::String& error);
    void updatePanel();
    void showIoPanel(EndBlock::Kind kind);
    void closeAllWindows();
    /// Index of the first split stage, or -1 when the chain is a single path.
    int firstSplitStage() const;

    BlockRigProcessor& mProcessor;
    juce::AudioDeviceManager* mDeviceManager;

    theme::Look mLook;

    juce::Label mTitle;
    CpuMeter mCpuMeter;
    HeaderMeters mHeaderMeters;
    TransportBar mTransportBar;
    juce::TextButton mSettingsButton{"Settings"};
    /// Global: mutes the rig and opens the tuner. Tuning happens mid-set, so it
    /// lives in the header rather than behind a menu.
    juce::TextButton mTunerButton{"Tuner"};
    juce::TextButton mRigButton{"Rig"};
    juce::Label mRigName;
    juce::File mCurrentRigFile;

    /// Big and always reachable: an app that opens a live input into a live
    /// output needs an obvious kill switch, not a menu item.
    juce::TextButton mMuteButton;

    /// Doubles as the call to action when no plug-ins have been scanned yet.
    juce::TextButton mPluginCountButton;

    LaneView mLane;

    /// Big, unmissable, clickable banner shown while the output is muted.
    /// A small button in the header was not enough: muted output is silence with
    /// no other symptom, and people reasonably conclude the app is broken.
    class MuteBanner final : public juce::Component
    {
    public:
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseEnter(const juce::MouseEvent&) override { repaint(); }
        void mouseExit(const juce::MouseEvent&) override { repaint(); }

        std::function<void()> onClick;
    };

    MuteBanner mMuteBanner;

    /// Holds the floating windows and dims the rig behind whichever one is
    /// active. Everything below the header lives under this.
    class WindowLayer final : public juce::Component
    {
    public:
        WindowLayer();

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;

        /// True when an unpinned window is open, which is what dims the backdrop.
        bool hasModalWindow() const;

        std::function<void()> onBackdropClicked;
    };

    void openBlockWindow(const juce::String& uid);
    void openUtilityWindow(juce::String title, BlockCategory category,
                           std::unique_ptr<juce::Component> content);
    void closeWindow(BlockWindow* window);
    void closeActiveWindow();
    void togglePin(BlockWindow* window);
    void layOutWindows();
    BlockWindow* findWindowForBlock(const juce::String& uid) const;

    static constexpr int kMaxPinnedWindows = 6;

    WindowLayer mWindowLayer;
    std::vector<std::unique_ptr<BlockWindow>> mWindows;

    /// Circled X at the top-left, which closes whatever window is open. Present
    /// even when a window covers little of the app, so there is always one
    /// obvious way out.
    class CircleCloseButton final : public juce::Button
    {
    public:
        CircleCloseButton() : juce::Button("Close") {}
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    };

    CircleCloseButton mCloseOverlayButton;

    juce::Label mCanvasHint;

    std::unique_ptr<juce::FileChooser> mFileChooser;
    juce::TooltipWindow mTooltips{this, 600};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainView)
};

} // namespace blockrig
