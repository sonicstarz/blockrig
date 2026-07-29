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
{
public:
    MainView(BlockRigProcessor& processor, juce::AudioDeviceManager* deviceManager);
    ~MainView() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;

    void showSettings();
    void updatePanel();
    void showIoPanel(EndBlock::Kind kind);

    BlockRigProcessor& mProcessor;
    juce::AudioDeviceManager* mDeviceManager;

    theme::Look mLook;
    PluginEditorWindows mEditorWindows;

    juce::Label mTitle;
    CpuMeter mCpuMeter;
    juce::TextButton mSettingsButton{"Settings"};

    LaneView mLane;

    /// Whatever the selected block wants to show: the NAM panel, a generic
    /// parameter view, or the I/O controls.
    std::unique_ptr<juce::Component> mPanel;
    juce::Label mPanelPlaceholder;

    juce::TooltipWindow mTooltips{this, 600};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainView)
};

} // namespace blockrig
