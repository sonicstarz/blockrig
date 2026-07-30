#pragma once

#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "BlockRigProcessor.h"
#include "ui/CpuMeter.h"
#include "ui/HeaderMeters.h"
#include "ui/LaneView.h"
#include "ui/PluginEditorWindows.h"
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

private:
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    void timerCallback() override;

    void showSettings();
    void startScan();
    void refreshHeader();
    void buildTabs();
    void updateLayoutLimits();
    void updatePanel();
    void showIoPanel(EndBlock::Kind kind);

    BlockRigProcessor& mProcessor;
    juce::AudioDeviceManager* mDeviceManager;

    theme::Look mLook;
    PluginEditorWindows mEditorWindows;

    juce::Label mTitle;
    CpuMeter mCpuMeter;
    HeaderMeters mHeaderMeters;
    juce::TextButton mSettingsButton{"Settings"};

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

    /// Hosts one child and sizes it to fill, so a tab's contents can be swapped
    /// without adding and removing tabs.
    class PanelHolder final : public juce::Component
    {
    public:
        void setPanel(std::unique_ptr<juce::Component> panel);
        void resized() override;

    private:
        std::unique_ptr<juce::Component> mPanel;
    };

    /// The bottom section: tabbed, and resizable by dragging the bar above it.
    juce::TabbedComponent mTabs{juce::TabbedButtonBar::TabsAtTop};
    PanelHolder mBlockTab;
    PanelHolder mInputTab;
    PanelHolder mOutputTab;
    juce::Label mPanelPlaceholder;

    juce::StretchableLayoutManager mLayout;
    std::unique_ptr<juce::StretchableLayoutResizerBar> mResizer;

    juce::TooltipWindow mTooltips{this, 600};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainView)
};

} // namespace blockrig
