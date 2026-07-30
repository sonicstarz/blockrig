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

        if (kind == EndBlock::Kind::output)
        {
            // Proves the route from here to the speakers without needing an
            // instrument, which separates "no output" from "no input".
            mTestTone.setButtonText("Test tone");
            mTestTone.setTooltip("Plays 440 Hz for two seconds, ignoring the chain and the mute. "
                                 "If you hear nothing, the problem is between this app and your speakers.");
            mTestTone.onClick = [this] { mProcessor.startTestTone(); };
            addAndMakeVisible(mTestTone);
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

        if (mKind == EndBlock::Kind::output)
        {
            controls.removeFromLeft(theme::metrics::gap);
            mTestTone.setBounds(controls.removeFromLeft(120).withSizeKeepingCentre(120, 30));
        }

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

        // The stock selector applies every change the instant it is made, which
        // is alarming when you are choosing between 34 channels on a live rig.
        // Wrap it so nothing sticks until Apply, and Revert puts it all back.
        class AudioSettingsPanel final : public juce::Component
        {
        public:
            explicit AudioSettingsPanel(juce::AudioDeviceManager& deviceManager)
                : mDeviceManager(deviceManager)
                , mOriginalSetup(deviceManager.getAudioDeviceSetup())
                , mOriginalDeviceType(deviceManager.getCurrentAudioDeviceType())
            {
                // showChannelsAsStereoPairs is off for inputs: a guitar is one
                // channel, and forcing pair selection makes that impossible.
                mSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
                    deviceManager, 1, 2, 0, 2, false, false, false, false);
                addAndMakeVisible(*mSelector);

                mNote.setText("Changes are previewed live. Apply keeps them; Revert restores what you had.",
                              juce::dontSendNotification);
                mNote.setFont(juce::FontOptions(11.5f));
                mNote.setColour(juce::Label::textColourId, theme::colours::textFaint);
                addAndMakeVisible(mNote);

                mApply.setButtonText("Apply");
                mApply.onClick = [this] { close(); };
                addAndMakeVisible(mApply);

                mRevert.setButtonText("Revert");
                mRevert.onClick = [this] {
                    if (mOriginalDeviceType.isNotEmpty()
                        && mDeviceManager.getCurrentAudioDeviceType() != mOriginalDeviceType)
                        mDeviceManager.setCurrentAudioDeviceType(mOriginalDeviceType, true);

                    mDeviceManager.setAudioDeviceSetup(mOriginalSetup, true);
                    close();
                };
                addAndMakeVisible(mRevert);

                setSize(520, 420);
            }

            void resized() override
            {
                auto area = getLocalBounds().reduced(theme::metrics::gap);
                auto buttons = area.removeFromBottom(34);
                mApply.setBounds(buttons.removeFromRight(90).reduced(2));
                mRevert.setBounds(buttons.removeFromRight(90).reduced(2));
                area.removeFromBottom(4);
                mNote.setBounds(area.removeFromBottom(18));
                mSelector->setBounds(area);
            }

            void paint(juce::Graphics& g) override { g.fillAll(theme::colours::background); }

        private:
            void close()
            {
                if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
                    dialog->exitModalState(0);
            }

            juce::AudioDeviceManager& mDeviceManager;
            juce::AudioDeviceManager::AudioDeviceSetup mOriginalSetup;
            juce::String mOriginalDeviceType;
            std::unique_ptr<juce::AudioDeviceSelectorComponent> mSelector;
            juce::Label mNote;
            juce::TextButton mApply, mRevert;
        };

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(new AudioSettingsPanel(*mDeviceManager));
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
    juce::TextButton mTestTone;
};

/// Controls for a split stage: how the two sides recombine, and each side's own
/// level and placement.
///
/// These did not exist before, so a user trying to balance the two sides of a
/// split had only the blocks' own trims - which change what each side sounds
/// like, not how much of it reaches the mix.
class SplitPanel final : public juce::Component
{
public:
    SplitPanel(BlockRigProcessor& processor, int stageIndex)
        : mProcessor(processor)
        , mStageIndex(stageIndex)
    {
        mTitle.setText("Parallel stage", juce::dontSendNotification);
        mTitle.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        addAndMakeVisible(mTitle);

        mModeLabel.setText("RECOMBINE", juce::dontSendNotification);
        mModeLabel.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        mModeLabel.setColour(juce::Label::textColourId, theme::colours::textFaint);
        addAndMakeVisible(mModeLabel);

        mMode.addItem("Dual mono - A left, B right", 1);
        mMode.addItem("Parallel - both sides summed in stereo", 2);
        mMode.setSelectedId(processor.getChain().getStageMode(stageIndex) == BlockChain::StageMode::dualMono
                                ? 1
                                : 2,
                            juce::dontSendNotification);
        mMode.onChange = [this] {
            mProcessor.getChain().setStageMode(mStageIndex,
                                               mMode.getSelectedId() == 1 ? BlockChain::StageMode::dualMono
                                                                          : BlockChain::StageMode::parallel);
            updateEnablement();
        };
        addAndMakeVisible(mMode);

        for (int row = 0; row < 2; ++row)
        {
            auto& side = mSides[static_cast<size_t>(row)];

            side.label.setText(row == 0 ? "SIDE A" : "SIDE B", juce::dontSendNotification);
            side.label.setFont(juce::FontOptions(10.5f, juce::Font::bold));
            side.label.setColour(juce::Label::textColourId, theme::colours::accent);
            addAndMakeVisible(side.label);

            side.gainLabel.setText("LEVEL", juce::dontSendNotification);
            side.gainLabel.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            side.gainLabel.setColour(juce::Label::textColourId, theme::colours::textFaint);
            addAndMakeVisible(side.gainLabel);

            side.gain.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            side.gain.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 15);
            side.gain.setRange(-40.0, 12.0, 0.1);
            side.gain.setValue(processor.getChain().getRowGainDb(stageIndex, row),
                               juce::dontSendNotification);
            side.gain.textFromValueFunction = [](double v) { return juce::String(v, 1) + " dB"; };
            side.gain.onValueChange = [this, row] {
                mProcessor.getChain().setRowGainDb(mStageIndex, row,
                                                   static_cast<float>(mSides[static_cast<size_t>(row)]
                                                                          .gain.getValue()));
            };
            addAndMakeVisible(side.gain);

            side.panLabel.setText("PAN", juce::dontSendNotification);
            side.panLabel.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            side.panLabel.setColour(juce::Label::textColourId, theme::colours::textFaint);
            addAndMakeVisible(side.panLabel);

            side.pan.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            side.pan.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 15);
            side.pan.setRange(-1.0, 1.0, 0.01);
            side.pan.setValue(processor.getChain().getRowPan(stageIndex, row), juce::dontSendNotification);
            side.pan.textFromValueFunction = [](double v) {
                if (std::abs(v) < 0.005)
                    return juce::String("C");
                return (v < 0.0 ? juce::String("L") : juce::String("R"))
                       + juce::String(juce::roundToInt(std::abs(v) * 100.0));
            };
            side.pan.onValueChange = [this, row] {
                mProcessor.getChain().setRowPan(mStageIndex, row,
                                                 static_cast<float>(mSides[static_cast<size_t>(row)]
                                                                        .pan.getValue()));
            };
            addAndMakeVisible(side.pan);
        }

        mHint.setFont(juce::FontOptions(11.0f));
        mHint.setColour(juce::Label::textColourId, theme::colours::textFaint);
        addAndMakeVisible(mHint);

        updateEnablement();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(theme::metrics::padding);

        auto header = area.removeFromTop(24);
        mTitle.setBounds(header.removeFromLeft(160));

        area.removeFromTop(6);
        auto modeRow = area.removeFromTop(40);
        mModeLabel.setBounds(modeRow.removeFromTop(13));
        mMode.setBounds(modeRow.removeFromTop(26).withWidth(juce::jmin(320, modeRow.getWidth())));

        area.removeFromTop(theme::metrics::gap);
        mHint.setBounds(area.removeFromBottom(18));

        for (auto& side : mSides)
        {
            auto row = area.removeFromTop(78);
            side.label.setBounds(row.removeFromLeft(64).withSizeKeepingCentre(64, 20));

            auto gainCell = row.removeFromLeft(84);
            side.gainLabel.setBounds(gainCell.removeFromTop(13));
            side.gain.setBounds(gainCell);

            auto panCell = row.removeFromLeft(84);
            side.panLabel.setBounds(panCell.removeFromTop(13));
            side.pan.setBounds(panCell);
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
    void updateEnablement()
    {
        const bool dualMono = mProcessor.getChain().getStageMode(mStageIndex)
                              == BlockChain::StageMode::dualMono;

        // In dual mono the sides *are* the channels, so a pan control would be
        // lying about what it does.
        for (auto& side : mSides)
        {
            side.pan.setEnabled(!dualMono);
            side.panLabel.setEnabled(!dualMono);
        }

        mHint.setText(dualMono ? "Side A feeds the left channel, side B the right."
                               : "Both sides receive the full stereo signal and are summed.",
                      juce::dontSendNotification);
    }

    struct Side
    {
        juce::Label label, gainLabel, panLabel;
        juce::Slider gain, pan;
    };

    BlockRigProcessor& mProcessor;
    int mStageIndex;
    juce::Label mTitle, mModeLabel, mHint;
    juce::ComboBox mMode;
    std::array<Side, 2> mSides;
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

namespace
{
/// Embeds a hosted plug-in's own editor in the Block tab.
///
/// A generic parameter list is a poor substitute for the interface a plug-in was
/// designed around, so the real editor goes here. Editors size themselves and are
/// often taller than the tab, hence the viewport.
class EmbeddedPluginEditor final : public juce::Component
{
public:
    EmbeddedPluginEditor(juce::AudioPluginInstance& plugin, std::function<void()> onDetach)
        : mOnDetach(std::move(onDetach))
    {
        mEditor.reset(plugin.createEditorIfNeeded());

        if (mEditor != nullptr)
        {
            mViewport.setViewedComponent(mEditor.get(), false);
            mViewport.setScrollBarsShown(true, true);
            addAndMakeVisible(mViewport);
        }

        mDetach.setButtonText("Open in window");
        mDetach.onClick = [this] {
            if (mOnDetach)
                mOnDetach();
        };
        addAndMakeVisible(mDetach);

        mName.setText(plugin.getName(), juce::dontSendNotification);
        mName.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        addAndMakeVisible(mName);
    }

    ~EmbeddedPluginEditor() override
    {
        // Detach before destruction: the editor belongs to the plug-in, and it
        // may be handed to a floating window next.
        mViewport.setViewedComponent(nullptr, false);
        mEditor.reset();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(theme::metrics::gap);
        auto header = area.removeFromTop(26);
        mDetach.setBounds(header.removeFromRight(140).reduced(2, 0));
        mName.setBounds(header);
        area.removeFromTop(4);
        mViewport.setBounds(area);
    }

    void paint(juce::Graphics& g) override { g.fillAll(theme::colours::panel); }

private:
    std::unique_ptr<juce::AudioProcessorEditor> mEditor;
    juce::Viewport mViewport;
    juce::TextButton mDetach;
    juce::Label mName;
    std::function<void()> mOnDetach;
};
} // namespace

//==============================================================================
void MainView::MuteBanner::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const bool hovered = isMouseOver();

    g.setColour(theme::colours::bad.withAlpha(hovered ? 0.28f : 0.20f));
    g.fillRoundedRectangle(bounds, theme::metrics::cornerRadius);
    g.setColour(theme::colours::bad.withAlpha(hovered ? 1.0f : 0.8f));
    g.drawRoundedRectangle(bounds, theme::metrics::cornerRadius, 2.0f);

    g.setColour(theme::colours::text);
    g.setFont(juce::FontOptions(17.0f, juce::Font::bold));
    g.drawText("OUTPUT IS MUTED", bounds.removeFromTop(bounds.getHeight() * 0.55f),
               juce::Justification::centredBottom, false);

    g.setColour(theme::colours::textDim);
    g.setFont(juce::FontOptions(13.0f));
    g.drawText("Click here to start hearing audio", bounds, juce::Justification::centredTop, false);
}

void MainView::MuteBanner::mouseDown(const juce::MouseEvent&)
{
    if (onClick)
        onClick();
}

//==============================================================================
void MainView::PanelHolder::setPanel(std::unique_ptr<juce::Component> panel)
{
    if (mPanel != nullptr)
        removeChildComponent(mPanel.get());

    mPanel = std::move(panel);

    if (mPanel != nullptr)
    {
        addAndMakeVisible(*mPanel);
        resized();
    }
}

void MainView::PanelHolder::resized()
{
    // Whatever is in here fills it: the swapped-in panel, or the placeholder.
    for (auto* child : getChildren())
        child->setBounds(getLocalBounds());
}

//==============================================================================
MainView::MainView(BlockRigProcessor& processor, juce::AudioDeviceManager* deviceManager)
    : mProcessor(processor)
    , mDeviceManager(deviceManager)
    , mCpuMeter(processor)
    , mHeaderMeters(processor)
    , mTransportBar(processor)
    , mLane(processor, mEditorWindows)
{
    setLookAndFeel(&mLook);

    mTitle.setText("BLOCKRIG", juce::dontSendNotification);
    mTitle.setFont(juce::FontOptions(17.0f, juce::Font::bold));
    mTitle.setColour(juce::Label::textColourId, theme::colours::accent);
    addAndMakeVisible(mTitle);

    addAndMakeVisible(mCpuMeter);
    addAndMakeVisible(mHeaderMeters);
    addAndMakeVisible(mTransportBar);

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

    mMuteBanner.onClick = [this] {
        mProcessor.setMuted(false);
        refreshHeader();
    };
    addAndMakeVisible(mMuteBanner);

    buildTabs();

    mPanelPlaceholder.setJustificationType(juce::Justification::centred);
    mPanelPlaceholder.setMinimumHorizontalScale(1.0f);
    mPanelPlaceholder.setColour(juce::Label::textColourId, theme::colours::textFaint);
    mPanelPlaceholder.setFont(juce::FontOptions(13.0f));
    mBlockTab.addAndMakeVisible(mPanelPlaceholder);

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
        // A split needs a second row, so the lane's minimum height changes.
        resized();
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

        mBlockTab.setPanel(nullptr);
        mPanelPlaceholder.setText(guidance, juce::dontSendNotification);
        mPanelPlaceholder.setVisible(true);
        mBlockTab.resized();
        return;
    }

    mPanelPlaceholder.setVisible(false);

    // Built-ins get a proper inline panel; third-party plug-ins get their own
    // editor in a floating window, with a generic view here as a stand-in.
    std::unique_ptr<juce::Component> panel;

    if (auto* nam = dynamic_cast<NamBlockProcessor*>(block->getPlugin()))
    {
        panel = std::make_unique<NamBlockPanel>(*nam);
    }
    else if (auto* plugin = block->getPlugin())
    {
        if (mEditorWindows.isOpen(uid))
        {
            // Its editor is in a floating window; an editor cannot live in two
            // places, so say so rather than showing an empty panel.
            auto note = std::make_unique<juce::Label>();
            note->setText(plugin->getName() + " is open in its own window.\n\n"
                              + "Close that window to bring the interface back here.",
                          juce::dontSendNotification);
            note->setJustificationType(juce::Justification::centred);
            note->setColour(juce::Label::textColourId, theme::colours::textFaint);
            panel = std::move(note);
        }
        else if (plugin->hasEditor())
        {
            panel = std::make_unique<EmbeddedPluginEditor>(*plugin, [this, uid] {
                if (auto* target = mProcessor.getChain().getBlockByUid(uid))
                    if (auto* pluginToShow = target->getPlugin())
                    {
                        // Drop the embedded copy first: one editor, one home.
                        mBlockTab.setPanel(nullptr);
                        mEditorWindows.show(uid, *pluginToShow);
                        mLane.refresh();
                        updatePanel();
                    }
            });
        }
        else
        {
            // No interface of its own; the parameter list is the honest fallback.
            panel = std::make_unique<juce::GenericAudioProcessorEditor>(*plugin);
        }
    }

    mBlockTab.setPanel(std::move(panel));

    // The Split tab follows the selection: it only means anything for a block
    // that is actually on one side of a split.
    const auto position = mProcessor.getChain().findBlock(uid);
    const bool inSplit = position.has_value() && mProcessor.getChain().isStageSplit(position->stage);

    if (inSplit)
        mSplitTab.setPanel(std::make_unique<SplitPanel>(mProcessor, position->stage));
    else
        mSplitTab.setPanel(nullptr);

    mTabs.setTabBackgroundColour(1, inSplit ? theme::colours::panel : theme::colours::background);

    if (mTabs.getCurrentTabIndex() != 1 || !inSplit)
        mTabs.setCurrentTabIndex(0);
}

void MainView::showIoPanel(EndBlock::Kind kind)
{
    mTabs.setCurrentTabIndex(kind == EndBlock::Kind::input ? 2 : 3);
}

void MainView::buildTabs()
{
    mTabs.setTabBarDepth(30);
    mTabs.setOutline(0);
    mTabs.setColour(juce::TabbedComponent::backgroundColourId, theme::colours::background);
    mTabs.setColour(juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);

    // Block first, since it is what the lane selection drives; the ends are
    // parked alongside so the user can leave them open while playing.
    mTabs.addTab("Block", theme::colours::panel, &mBlockTab, false);
    mTabs.addTab("Split", theme::colours::panel, &mSplitTab, false);
    mTabs.addTab("Input", theme::colours::panel, &mInputTab, false);
    mTabs.addTab("Output", theme::colours::panel, &mOutputTab, false);

    mInputTab.setPanel(std::make_unique<IoPanel>(mProcessor, EndBlock::Kind::input, mDeviceManager));
    mOutputTab.setPanel(std::make_unique<IoPanel>(mProcessor, EndBlock::Kind::output, mDeviceManager));

    mSplitPlaceholder.setText("Select a block on a split stage to balance its two sides.",
                              juce::dontSendNotification);
    mSplitPlaceholder.setJustificationType(juce::Justification::centred);
    mSplitPlaceholder.setColour(juce::Label::textColourId, theme::colours::textFaint);
    mSplitTab.addAndMakeVisible(mSplitPlaceholder);

    addAndMakeVisible(mTabs);

    // Draggable divider between the lane and the tabs.
    mResizer = std::make_unique<juce::StretchableLayoutResizerBar>(&mLayout, 1, false);
    addAndMakeVisible(*mResizer);

    updateLayoutLimits();
}

void MainView::updateLayoutLimits()
{
    // The lane never shrinks below what its rows need, so a split cannot be
    // clipped; everything above that is the user's to divide.
    const auto laneMinimum = static_cast<double>(mLane.getPreferredHeight());

    mLayout.setItemLayout(0, laneMinimum, 520.0, laneMinimum);
    mLayout.setItemLayout(1, 7.0, 7.0, 7.0);
    mLayout.setItemLayout(2, 120.0, -1.0, -1.0);
}

void MainView::timerCallback()
{
    refreshHeader();
}

void MainView::refreshHeader()
{
    const bool muted = mProcessor.isMuted();
    // Naming the action rather than the state: "MUTED" reads as a status label,
    // and people sat waiting for sound that was never going to come.
    mMuteButton.setButtonText(muted ? "MUTED - CLICK TO PLAY" : "LIVE - CLICK TO MUTE");
    mMuteButton.setColour(juce::TextButton::buttonColourId,
                          muted ? theme::colours::bad.withAlpha(0.9f) : theme::colours::good.withAlpha(0.7f));

    mMuteBanner.setVisible(muted);
    if (muted)
        mMuteBanner.toFront(false);

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
    //
    // Owning a device manager is what identifies the app. wrapperType is NOT
    // usable here: the app constructs the processor directly, so JUCE reports
    // wrapperType_Undefined rather than _Standalone.
    if (mDeviceManager == nullptr)
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

    mMuteButton.setBounds(header.removeFromLeft(178).withSizeKeepingCentre(178, 28));
    header.removeFromLeft(theme::metrics::gap);
    mHeaderMeters.setBounds(header.removeFromLeft(190).withSizeKeepingCentre(190, 34));

    mSettingsButton.setBounds(header.removeFromRight(88).withSizeKeepingCentre(88, 26));
    header.removeFromRight(theme::metrics::gap);
    mCpuMeter.setBounds(header.removeFromRight(126).withSizeKeepingCentre(126, 30));
    header.removeFromRight(theme::metrics::gap);
    mPluginCountButton.setBounds(header.removeFromRight(110).withSizeKeepingCentre(110, 26));
    header.removeFromRight(theme::metrics::gap);
    mTransportBar.setBounds(header.removeFromRight(214).withSizeKeepingCentre(214, 38));

    area.removeFromTop(theme::metrics::gap);
    area = area.reduced(theme::metrics::padding, 0);
    area.removeFromBottom(theme::metrics::padding);

    updateLayoutLimits();

    // Lane on top, draggable bar, tabs below — the user sets the balance.
    juce::Component* items[] = {&mLane, mResizer.get(), &mTabs};
    mLayout.layOutComponents(items, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true,
                             true);

    mMuteBanner.setBounds(mLane.getBounds().withSizeKeepingCentre(
        juce::jmin(430, mLane.getWidth() - 20), juce::jmin(78, mLane.getHeight() - 8)));

}

} // namespace blockrig
