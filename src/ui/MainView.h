#pragma once

#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "BlockRigProcessor.h"
#include "ui/CpuMeter.h"
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
    void updatePanel();
    void showIoPanel(EndBlock::Kind kind);

    BlockRigProcessor& mProcessor;
    juce::AudioDeviceManager* mDeviceManager;

    theme::Look mLook;
    PluginEditorWindows mEditorWindows;

    juce::Label mTitle;
    CpuMeter mCpuMeter;
    juce::TextButton mSettingsButton{"Settings"};

    /// Big and always reachable: an app that opens a live input into a live
    /// output needs an obvious kill switch, not a menu item.
    juce::TextButton mMuteButton;

    /// Doubles as the call to action when no plug-ins have been scanned yet.
    juce::TextButton mPluginCountButton;

    LaneView mLane;

    /// Whatever the selected block wants to show: the NAM panel, a generic
    /// parameter view, or the I/O controls.
    std::unique_ptr<juce::Component> mPanel;
    juce::Label mPanelPlaceholder;

    juce::TooltipWindow mTooltips{this, 600};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainView)
};

} // namespace blockrig
