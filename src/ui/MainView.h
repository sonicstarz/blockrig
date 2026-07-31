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
#include "state/Setlist.h"
#include "ui/GigView.h"
#include "ui/SnapshotStrip.h"
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

    /// Wired by the shell: clicking the wordmark goes back to the home screen.
    std::function<void()> onHomeRequested;

    static juce::File getRigsFolder();

    /// Loads a setlist and returns its first existing rig, or a null file. The
    /// home screen uses this so clicking a setlist opens night-one song-one.
    juce::File activateSetlist(const juce::File& setlistFile);
    void confirmThenSwitch(std::function<void()> proceed);
    void loadRigFile(const juce::File& file);

private:
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    void timerCallback() override;
    void mouseUp(const juce::MouseEvent&) override;

    void showSettings();
    void startScan();
    void refreshHeader();
    void showRigMenu();
    void saveRig(bool forceChooser);
    void openRig();

    /// Rig files management. The rigs folder is user-visible in Documents so
    /// rigs can be backed up, shared, and renamed like any other files.
    juce::Array<juce::File> listRigs() const;
    void showSetlistMenu();
    void enterGigView();
    void exitGigView();
    void stepRig(int direction);
    void newRig();
    bool isDirty() const { return mDirty; }
    void markSavedState();
    void showTuner(bool shouldBeOpen);
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
    juce::TextButton mSaveButton{"Save"};
    juce::TextButton mGigButton{"Gig"};

    /// When a setlist is loaded it, not the folder, decides rig order.
    Setlist mSetlist;
    bool mHasSetlist = false;

    std::unique_ptr<GigView> mGigView;
    juce::TextButton mPrevRig{"<"}, mNextRig{">"};

    /// Centre of the header: the rig's name, asterisked while there are unsaved
    /// changes. Clicking it opens the rig folder as a menu.
    class RigNameButton final : public juce::Component
    {
    public:
        void set(juce::String name, bool dirty)
        {
            mName = std::move(name);
            mDirty = dirty;
            repaint();
        }

        std::function<void()> onClick;

        void mouseUp(const juce::MouseEvent& event) override
        {
            if (contains(event.getPosition()) && onClick)
                onClick();
        }

        void paint(juce::Graphics& g) override;

    private:
        juce::String mName;
        bool mDirty = false;
    };

    RigNameButton mRigName;
    juce::File mCurrentRigFile;
    juce::String mSavedStateXml;
    bool mDirty = false;
    int mDirtyCheckCountdown = 0;
    int mShownSnapshotIndex = -1;

    SnapshotStrip mSnapshots;

    /// Big and always reachable: an app that opens a live input into a live
    /// output needs an obvious kill switch, not a menu item.
    juce::TextButton mMuteButton;


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
