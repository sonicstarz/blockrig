#include "ui/MainView.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include "blocks/nam/NamBlockProcessor.h"
#include "ui/NamBlockPanel.h"

namespace blockrig
{
namespace
{
/// Controls for the Input or Output end of the lane: what the chain is fed from
/// and what it drives. In the plug-in build the device side is the host's job, so
/// only the trim is offered.
class IoPanel final : public juce::Component
{
public:
    IoPanel(BlockRigProcessor& processor, EndBlock::Kind kind, juce::AudioDeviceManager* deviceManager)
        : mProcessor(processor)
        , mKind(kind)
        , mDeviceManager(deviceManager)
    {
        mTitle.setText(kind == EndBlock::Kind::input ? "Input" : "Output", juce::dontSendNotification);
        mTitle.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        addAndMakeVisible(mTitle);

        mDescription.setFont(juce::FontOptions(11.5f));
        mDescription.setColour(juce::Label::textColourId, theme::colours::textFaint);
        addAndMakeVisible(mDescription);

        mTrim.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        mTrim.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 17);
        mTrim.setRange(-24.0, 24.0, 0.1);
        mTrim.setValue(kind == EndBlock::Kind::input ? processor.getInputGainDb()
                                                     : processor.getOutputGainDb(),
                       juce::dontSendNotification);
        mTrim.textFromValueFunction = [](double value) { return juce::String(value, 1) + " dB"; };
        mTrim.onValueChange = [this] {
            if (mKind == EndBlock::Kind::input)
                mProcessor.setInputGainDb(static_cast<float>(mTrim.getValue()));
            else
                mProcessor.setOutputGainDb(static_cast<float>(mTrim.getValue()));
        };
        addAndMakeVisible(mTrim);

        mTrimLabel.setText("TRIM", juce::dontSendNotification);
        mTrimLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        mTrimLabel.setJustificationType(juce::Justification::centred);
        mTrimLabel.setColour(juce::Label::textColourId, theme::colours::textDim);
        addAndMakeVisible(mTrimLabel);

        if (kind == EndBlock::Kind::input)
        {
            // Mono is the guitarist's case and must be one click away.
            mModeLabel.setText("MODE", juce::dontSendNotification);
            mModeLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            mModeLabel.setColour(juce::Label::textColourId, theme::colours::textDim);
            addAndMakeVisible(mModeLabel);

            mMode.addItemList({"Mono (feeds both sides)", "Stereo"}, 1);
            mMode.setSelectedId(processor.getInputMode() == BlockRigProcessor::InputMode::mono ? 1 : 2,
                                juce::dontSendNotification);
            mMode.onChange = [this] {
                mProcessor.setInputMode(mMode.getSelectedId() == 1 ? BlockRigProcessor::InputMode::mono
                                                                   : BlockRigProcessor::InputMode::stereo);
            };
            addAndMakeVisible(mMode);
        }

        if (mDeviceManager != nullptr)
        {
            mDeviceButton.setButtonText("Audio settings...");
            mDeviceButton.onClick = [this] { showDeviceSettings(); };
            addAndMakeVisible(mDeviceButton);
        }

        updateDescription();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(theme::metrics::padding);

        auto header = area.removeFromTop(44);
        if (mDeviceManager != nullptr)
            mDeviceButton.setBounds(header.removeFromRight(150).reduced(2, 9));

        mTitle.setBounds(header.removeFromTop(22));
        mDescription.setBounds(header);

        area.removeFromTop(theme::metrics::gap);

        auto controls = area.removeFromTop(86);
        auto trimCell = controls.removeFromLeft(90);
        mTrimLabel.setBounds(trimCell.removeFromTop(13));
        mTrim.setBounds(trimCell);

        if (mKind == EndBlock::Kind::input)
        {
            controls.removeFromLeft(theme::metrics::gap);
            auto modeCell = controls.removeFromLeft(230).withTrimmedTop(4);
            mModeLabel.setBounds(modeCell.removeFromTop(14));
            mMode.setBounds(modeCell.removeFromTop(28));
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(theme::colours::panel);
        g.fillRoundedRectangle(bounds, theme::metrics::cornerRadius);
        g.setColour(theme::colours::outline);
        g.drawRoundedRectangle(bounds, theme::metrics::cornerRadius, 1.0f);
    }

private:
    void updateDescription()
    {
        if (mDeviceManager == nullptr)
        {
            mDescription.setText(mKind == EndBlock::Kind::input ? "Fed by the host's input bus"
                                                                : "Feeds the host's output bus",
                                 juce::dontSendNotification);
            return;
        }

        if (auto* device = mDeviceManager->getCurrentAudioDevice())
        {
            const auto channels = mKind == EndBlock::Kind::input ? device->getInputChannelNames()
                                                                 : device->getOutputChannelNames();
            mDescription.setText(device->getName() + "   •   " + juce::String(channels.size()) + " channels",
                                 juce::dontSendNotification);
        }
        else
        {
            mDescription.setText("No audio device selected", juce::dontSendNotification);
        }
    }

    void showDeviceSettings()
    {
        if (mDeviceManager == nullptr)
            return;

        auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(*mDeviceManager, 0, 2, 0, 2, false,
                                                                            false, true, false);
        selector->setSize(460, 320);

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(selector.release());
        options.dialogTitle = "Audio settings";
        options.dialogBackgroundColour = theme::colours::background;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }

    BlockRigProcessor& mProcessor;
    EndBlock::Kind mKind;
    juce::AudioDeviceManager* mDeviceManager;

    juce::Label mTitle, mDescription, mTrimLabel, mModeLabel;
    juce::Slider mTrim;
    juce::ComboBox mMode;
    juce::TextButton mDeviceButton;
};

/// Runs a plug-in scan on its own thread behind a progress window.
///
/// Scanning loads every plug-in on the machine in a child process and takes
/// minutes, so it cannot block the message thread. Deletes itself when done.
class ScanJob final : public juce::ThreadWithProgressWindow
{
public:
    ScanJob(PluginCatalog& catalog, std::function<void()> onComplete)
        : juce::ThreadWithProgressWindow("Scanning plug-ins", true, true)
        , mCatalog(catalog)
        , mOnComplete(std::move(onComplete))
    {
        setStatusMessage("Looking for plug-ins...");
    }

    void run() override
    {
        mSummary = mCatalog.scanAllFormats(
            [this](const PluginCatalog::ScanProgress& progress) {
                if (progress.total > 0)
                    setProgress(static_cast<double>(progress.scanned) / progress.total);

                setStatusMessage(juce::String(progress.scanned) + " of " + juce::String(progress.total)
                                 + "   •   " + juce::String(progress.found) + " found\n"
                                 + progress.currentPluginName);
            },
            [this] { return threadShouldExit(); });
    }

    void threadComplete(bool userPressedCancel) override
    {
        juce::String message;

        if (userPressedCancel)
            message = "Scan cancelled. " + juce::String(mSummary.found) + " plug-ins were added before stopping.";
        else
            message = juce::String(mSummary.found) + " plug-ins found from " + juce::String(mSummary.scanned)
                      + " scanned.";

        if (mSummary.denylisted > 0)
            message += "\n\n" + juce::String(mSummary.denylisted)
                       + " plug-in(s) crashed or hung and were skipped:\n"
                       + mSummary.denylistedNames.joinIntoString("\n");

        juce::NativeMessageBox::showAsync(juce::MessageBoxOptions()
                                              .withIconType(juce::MessageBoxIconType::InfoIcon)
                                              .withTitle("Plug-in scan")
                                              .withMessage(message)
                                              .withButton("OK"),
                                          nullptr);

        if (mOnComplete)
            mOnComplete();

        delete this;
    }

private:
    PluginCatalog& mCatalog;
    std::function<void()> mOnComplete;
    PluginCatalog::ScanSummary mSummary;
};
} // namespace

MainView::MainView(BlockRigProcessor& processor, juce::AudioDeviceManager* deviceManager)
    : mProcessor(processor)
    , mDeviceManager(deviceManager)
    , mCpuMeter(processor)
    , mLane(processor, mEditorWindows)
{
    setLookAndFeel(&mLook);

    mTitle.setText("BLOCKRIG", juce::dontSendNotification);
    mTitle.setFont(juce::FontOptions(17.0f, juce::Font::bold));
    mTitle.setColour(juce::Label::textColourId, theme::colours::accent);
    addAndMakeVisible(mTitle);

    addAndMakeVisible(mCpuMeter);

    // Muted at startup: opening a live input into a live output can howl before
    // the user has done anything, and they need one obvious way to stop it.
    mMuteButton.onClick = [this] {
        mProcessor.setMuted(!mProcessor.isMuted());
        refreshHeader();
    };
    mMuteButton.setTooltip("Mute the rig's output. Starts muted so nothing can feed back unexpectedly.");
    addAndMakeVisible(mMuteButton);

    // Shows how many blocks are available, and becomes the prompt to scan when
    // nothing has been scanned yet.
    mPluginCountButton.onClick = [this] { startScan(); };
    addAndMakeVisible(mPluginCountButton);

    mSettingsButton.onClick = [this] { showSettings(); };
    addAndMakeVisible(mSettingsButton);

    addAndMakeVisible(mLane);

    mPanelPlaceholder.setJustificationType(juce::Justification::centred);
    mPanelPlaceholder.setMinimumHorizontalScale(1.0f);
    mPanelPlaceholder.setColour(juce::Label::textColourId, theme::colours::textFaint);
    mPanelPlaceholder.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(mPanelPlaceholder);

    // The end blocks should say what they are wired to, not just "INPUT".
    if (mDeviceManager != nullptr)
    {
        if (auto* device = mDeviceManager->getCurrentAudioDevice())
        {
            const auto mode = mProcessor.getInputMode() == BlockRigProcessor::InputMode::mono ? "mono" : "stereo";
            mLane.setInputCaption(device->getName() + "\n" + mode);
            mLane.setOutputCaption(device->getName());
        }
        else
        {
            mLane.setInputCaption("No device\nclick to set up");
            mLane.setOutputCaption("No device");
        }
    }
    else
    {
        mLane.setInputCaption("From DAW");
        mLane.setOutputCaption("To DAW");
    }

    mLane.onSelectionChanged = [this] { updatePanel(); };
    mLane.onEndBlockSelected = [this](EndBlock::Kind kind) { showIoPanel(kind); };

    mProcessor.onChainChanged = [this] {
        mLane.refresh();
        updatePanel();
    };

    addKeyListener(this);
    setWantsKeyboardFocus(true);
    refreshHeader();
    startTimerHz(4);
    setSize(1180, 660);
}

MainView::~MainView()
{
    stopTimer();
    removeKeyListener(this);
    mProcessor.onChainChanged = nullptr;
    setLookAndFeel(nullptr);
}

bool MainView::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    // Escape closes the frontmost plug-in editor; window sprawl is the standard
    // complaint about hosts, so this is worth having.
    if (key == juce::KeyPress::escapeKey)
    {
        mEditorWindows.closeFrontmost();
        mLane.refresh();
        return true;
    }

    if (key == juce::KeyPress('w', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0))
    {
        mEditorWindows.closeAll();
        mLane.refresh();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        const auto uid = mLane.getSelectedUid();
        if (uid.isNotEmpty())
        {
            mEditorWindows.close(uid);
            mProcessor.removeBlock(uid);
            return true;
        }
    }

    return false;
}

void MainView::updatePanel()
{
    mPanel.reset();

    const auto uid = mLane.getSelectedUid();
    auto* block = uid.isNotEmpty() ? mProcessor.getChain().getBlockByUid(uid) : nullptr;

    if (block == nullptr)
    {
        const bool emptyLane = mProcessor.getChain().getNumBlocks() == 0;
        const bool noPlugins = mProcessor.getCatalog().getKnownPluginList().getNumTypes() == 0;

        juce::String guidance;
        if (emptyLane)
            guidance = "Your chain is empty.\n\nClick  +  in the lane above to add a block.\n"
                       "Start with NAM under Built-in, then drop a .nam capture on it.";
        else
            guidance = "Click a block in the lane to edit it.\n\n"
                       "Drag blocks to reorder  •  click a block's dot to bypass  •  "
                       "double-click a plug-in to open its window.";

        if (noPlugins)
            guidance += "\n\nNo plug-ins scanned yet — use  Scan plug-ins  in the header to find your VSTs.";

        if (mProcessor.isMuted())
            guidance += "\n\nOutput is MUTED. Click the red button in the header when you are ready to hear it.";

        mPanelPlaceholder.setText(guidance, juce::dontSendNotification);
        mPanelPlaceholder.setVisible(true);
        resized();
        return;
    }

    mPanelPlaceholder.setVisible(false);

    // Built-ins get a proper inline panel; third-party plug-ins get their own
    // editor in a floating window, with a generic view here as a stand-in.
    if (auto* nam = dynamic_cast<NamBlockProcessor*>(block->getPlugin()))
    {
        mPanel = std::make_unique<NamBlockPanel>(*nam);
    }
    else if (auto* plugin = block->getPlugin())
    {
        mPanel = std::make_unique<juce::GenericAudioProcessorEditor>(*plugin);
    }

    if (mPanel != nullptr)
        addAndMakeVisible(*mPanel);

    resized();
}

void MainView::showIoPanel(EndBlock::Kind kind)
{
    mPanel.reset();
    mPanelPlaceholder.setVisible(false);
    mPanel = std::make_unique<IoPanel>(mProcessor, kind, mDeviceManager);
    addAndMakeVisible(*mPanel);
    resized();
}

void MainView::timerCallback()
{
    refreshHeader();
}

void MainView::refreshHeader()
{
    const bool muted = mProcessor.isMuted();
    mMuteButton.setButtonText(muted ? "MUTED" : "LIVE");
    mMuteButton.setColour(juce::TextButton::buttonColourId,
                          muted ? theme::colours::bad.withAlpha(0.85f) : theme::colours::good.withAlpha(0.7f));

    const int count = mProcessor.getCatalog().getKnownPluginList().getNumTypes();

    if (count == 0)
    {
        mPluginCountButton.setButtonText("Scan plug-ins");
        mPluginCountButton.setColour(juce::TextButton::buttonColourId, theme::colours::accent.withAlpha(0.85f));
        mPluginCountButton.setTooltip("No plug-ins found yet. Scanning takes a few minutes and only needs "
                                     "doing once.");
    }
    else
    {
        mPluginCountButton.setButtonText(juce::String(count) + " plug-ins");
        mPluginCountButton.setColour(juce::TextButton::buttonColourId, theme::colours::panelRaised);
        mPluginCountButton.setTooltip("Click to rescan.");
    }
}

void MainView::startScan()
{
    // Scanning relaunches this executable as a child process for each plug-in.
    // That works from the app; inside a DAW the executable is the host's, so the
    // scan has to happen in the app instead.
    if (mProcessor.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
    {
        juce::NativeMessageBox::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("Scan in the BlockRig app")
                .withMessage("Scanning loads every plug-in on this machine in a separate process, which needs "
                             "the BlockRig app rather than a plug-in inside a DAW.\n\nOpen BlockRig, scan "
                             "there, and this plug-in will pick up the same list.")
                .withButton("OK"),
            nullptr);
        return;
    }

    // Owns itself; deletes on completion.
    auto* job = new ScanJob(mProcessor.getCatalog(), [this] {
        mProcessor.getCatalog().saveToStorage();
    });

    job->launchThread();
}

void MainView::showSettings()
{
    juce::PopupMenu menu;

    menu.addItem(1, "Rescan plug-ins...");
    menu.addItem(2, "Keep plug-in editors on top", true, mEditorWindows.getAlwaysOnTop());
    menu.addItem(3, "Close all plug-in editors");
    menu.addSeparator();

    const auto denylisted = mProcessor.getCatalog().getDenylist().size();
    menu.addItem(4, "Clear denylist (" + juce::String(denylisted) + ")", denylisted > 0);
    menu.addSeparator();
    menu.addItem(juce::PopupMenu::Item(juce::String(mProcessor.getCatalog().getKnownPluginList().getNumTypes())
                                       + " plug-ins available")
                     .setEnabled(false));

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&mSettingsButton), [this](int choice) {
        switch (choice)
        {
            case 1: startScan(); break;
            case 2:
                mEditorWindows.setAlwaysOnTop(!mEditorWindows.getAlwaysOnTop());
                break;
            case 3:
                mEditorWindows.closeAll();
                mLane.refresh();
                break;
            case 4:
                mProcessor.getCatalog().clearDenylist();
                break;
            default: break;
        }
    });
}

void MainView::paint(juce::Graphics& g)
{
    g.fillAll(theme::colours::background);

    // Header divider.
    g.setColour(theme::colours::outline);
    g.drawHorizontalLine(theme::metrics::headerHeight, 0.0f, static_cast<float>(getWidth()));
}

void MainView::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop(theme::metrics::headerHeight).reduced(theme::metrics::padding, 0);
    mTitle.setBounds(header.removeFromLeft(112).withSizeKeepingCentre(112, 24));

    mMuteButton.setBounds(header.removeFromLeft(78).withSizeKeepingCentre(78, 28));
    header.removeFromLeft(theme::metrics::gap);

    mSettingsButton.setBounds(header.removeFromRight(88).withSizeKeepingCentre(88, 26));
    header.removeFromRight(theme::metrics::gap);
    mCpuMeter.setBounds(header.removeFromRight(130).withSizeKeepingCentre(130, 30));
    header.removeFromRight(theme::metrics::gap);
    mPluginCountButton.setBounds(header.removeFromRight(116).withSizeKeepingCentre(116, 26));

    area.removeFromTop(theme::metrics::gap);
    mLane.setBounds(area.removeFromTop(theme::metrics::laneHeight).reduced(theme::metrics::padding, 0));
    area.removeFromTop(theme::metrics::gap);

    auto panelArea = area.reduced(theme::metrics::padding).withTrimmedTop(0);
    mPanelPlaceholder.setBounds(panelArea);

    if (mPanel != nullptr)
        mPanel->setBounds(panelArea);
}

} // namespace blockrig
