#include "ui/MainView.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include "blocks/nam/NamBlockProcessor.h"
#include "ui/NamBlockPanel.h"

namespace blockrig
{
namespace
{
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


/// Holds a hosted plug-in's own editor.
///
/// Owns it, because createEditorIfNeeded() hands over a pointer the caller must
/// delete - the processor only keeps a weak reference. Watches it for size
/// changes too: plug-ins with resizable interfaces change their own bounds, and
/// the window around them has to follow or the interface gets clipped.
class HostedPluginEditor final : public juce::Component
                               , private juce::ComponentListener
{
public:
    explicit HostedPluginEditor(std::unique_ptr<juce::AudioProcessorEditor> editor)
        : mEditor(std::move(editor))
    {
        addAndMakeVisible(*mEditor);
        mEditor->setTopLeftPosition(0, 0);
        mEditor->addComponentListener(this);
        setSize(mEditor->getWidth(), mEditor->getHeight());
    }

    ~HostedPluginEditor() override
    {
        mEditor->removeComponentListener(this);
    }

    void resized() override
    {
        // Centre rather than stretch: an editor that is not resizable must not be
        // distorted to fill the window.
        mEditor->setTopLeftPosition((getWidth() - mEditor->getWidth()) / 2,
                                    juce::jmax(0, (getHeight() - mEditor->getHeight()) / 2));
    }

    std::function<void(int, int)> onEditorResized;

private:
    void componentMovedOrResized(juce::Component&, bool, bool wasResized) override
    {
        if (wasResized && onEditorResized)
            onEditorResized(mEditor->getWidth(), mEditor->getHeight());
    }

    std::unique_ptr<juce::AudioProcessorEditor> mEditor;
};

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
MainView::WindowLayer::WindowLayer()
{
    setInterceptsMouseClicks(false, true); // transparent until a window dims it
}

bool MainView::WindowLayer::hasModalWindow() const
{
    for (auto* child : getChildren())
        if (auto* window = dynamic_cast<BlockWindow*>(child))
            if (!window->isPinned())
                return true;

    return false;
}

void MainView::WindowLayer::paint(juce::Graphics& g)
{
    // Dim the rig behind an open window so attention lands on the window, while
    // the header above stays legible and usable.
    if (hasModalWindow())
        g.fillAll(juce::Colours::black.withAlpha(0.55f));
}

void MainView::CircleCloseButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    const auto emphasis = down ? 1.0f : (highlighted ? 0.85f : 0.6f);

    g.setColour(theme::colours::panel.withAlpha(0.9f));
    g.fillEllipse(bounds);
    g.setColour(theme::colours::textDim.withAlpha(emphasis));
    g.drawEllipse(bounds, 1.4f);

    auto cross = bounds.reduced(bounds.getWidth() * 0.32f);
    g.setColour(theme::colours::text.withAlpha(emphasis));
    g.drawLine(cross.getX(), cross.getY(), cross.getRight(), cross.getBottom(), 1.6f);
    g.drawLine(cross.getX(), cross.getBottom(), cross.getRight(), cross.getY(), 1.6f);
}

void MainView::WindowLayer::mouseDown(const juce::MouseEvent&)
{
    // Clicking the dimmed area is the other obvious way to dismiss.
    if (hasModalWindow() && onBackdropClicked)
        onBackdropClicked();
}

//==============================================================================
MainView::MainView(BlockRigProcessor& processor, juce::AudioDeviceManager* deviceManager)
    : mProcessor(processor)
    , mDeviceManager(deviceManager)
    , mCpuMeter(processor)
    , mHeaderMeters(processor)
    , mTransportBar(processor)
    , mLane(processor)
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

    mRigButton.onClick = [this] { showRigMenu(); };
    addAndMakeVisible(mRigButton);

    mRigName.setFont(juce::FontOptions(12.5f));
    mRigName.setColour(juce::Label::textColourId, theme::colours::textDim);
    mRigName.setJustificationType(juce::Justification::centredLeft);
    mRigName.setText("Untitled rig", juce::dontSendNotification);
    addAndMakeVisible(mRigName);

    addAndMakeVisible(mLane);

    mMuteBanner.onClick = [this] {
        mProcessor.setMuted(false);
        refreshHeader();
    };
    addAndMakeVisible(mMuteBanner);

    mCanvasHint.setFont(juce::FontOptions(12.5f));
    mCanvasHint.setColour(juce::Label::textColourId, theme::colours::textFaint);
    mCanvasHint.setJustificationType(juce::Justification::centred);
    mCanvasHint.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(mCanvasHint);

    // Above the lane, so an open window sits over the rig and the dim covers it.
    addAndMakeVisible(mWindowLayer);
    mWindowLayer.onBackdropClicked = [this] { closeActiveWindow(); };

    // Above the layer again: the way out must never be behind the dim.
    mCloseOverlayButton.onClick = [this] { closeActiveWindow(); };
    mCloseOverlayButton.setTooltip("Close the open block window");
    mCloseOverlayButton.setVisible(false);
    addAndMakeVisible(mCloseOverlayButton);




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

    // A block opens as a window rather than filling a panel below the lane.
    mLane.onBlockActivated = [this](juce::String uid) { openBlockWindow(uid); };
    mLane.isBlockWindowOpen = [this](const juce::String& uid) { return findWindowForBlock(uid) != nullptr; };

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
        closeActiveWindow();
        return true;
    }

    if (key == juce::KeyPress('w', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0))
    {
        closeAllWindows();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        const auto uid = mLane.getSelectedUid();
        if (uid.isNotEmpty())
        {
            if (auto* window = findWindowForBlock(uid))
                closeWindow(window);
            mProcessor.removeBlock(uid);
            return true;
        }
    }

    return false;
}

void MainView::updatePanel()
{
    // Selection no longer swaps a panel: blocks are opened as windows. Keep the
    // canvas hint honest about what to do next.
    const bool noPlugins = mProcessor.getCatalog().getKnownPluginList().getNumTypes() == 0;
    const bool emptyLane = mProcessor.getChain().getNumBlocks() == 0;

    juce::String hint;

    if (emptyLane)
        hint = "Your chain is empty. Click  +  in the lane to add a block.";
    else
        hint = "Click a block to open it. Pin windows here to keep them open.";

    if (noPlugins)
        hint += "\n\nNo plug-ins scanned yet — use  Scan plug-ins  in the header.";

    if (mProcessor.isMuted())
        hint += "\n\nOutput is muted.";

    // Once something is pinned the canvas speaks for itself.
    bool anyPinned = false;
    for (const auto& window : mWindows)
        if (window->isPinned())
            anyPinned = true;

    mCanvasHint.setText(anyPinned ? juce::String() : hint, juce::dontSendNotification);
}

BlockWindow* MainView::findWindowForBlock(const juce::String& uid) const
{
    for (const auto& window : mWindows)
        if (window->blockUid == uid)
            return window.get();

    return nullptr;
}

void MainView::openBlockWindow(const juce::String& uid)
{
    // Already open: bring it forward rather than making a duplicate.
    if (auto* existing = findWindowForBlock(uid))
    {
        existing->toFront(true);
        return;
    }

    auto* block = mProcessor.getChain().getBlockByUid(uid);
    if (block == nullptr)
        return;

    auto* plugin = block->getPlugin();
    if (plugin == nullptr)
        return;

    juce::PluginDescription description;
    plugin->fillInPluginDescription(description);
    const auto category = categoriseBlock(description);

    std::unique_ptr<juce::Component> content;
    juce::String subtitle;
    int width = BlockWindow::kDefaultWidth;
    int height = BlockWindow::kDefaultHeight;

    if (auto* nam = dynamic_cast<NamBlockProcessor*>(plugin))
    {
        content = std::make_unique<NamBlockPanel>(*nam);
        width = 700;
        height = 200 + BlockWindow::kTitleBarHeight;
    }
    else if (plugin->hasEditor())
    {
        // The plug-in's own interface, sized to whatever it asks for.
        if (auto* editor = plugin->createEditorIfNeeded())
        {
            width = juce::jlimit(280, 1200, editor->getWidth());
            height = juce::jlimit(180, 900, editor->getHeight()) + BlockWindow::kTitleBarHeight;

            auto holder = std::make_unique<HostedPluginEditor>(
                std::unique_ptr<juce::AudioProcessorEditor>(editor));

            holder->onEditorResized = [this, uid](int editorWidth, int editorHeight) {
                if (auto* window = findWindowForBlock(uid))
                    window->setSize(juce::jlimit(280, 1200, editorWidth),
                                    juce::jlimit(180, 900, editorHeight) + BlockWindow::kTitleBarHeight);
            };

            content = std::move(holder);
        }
    }

    if (content == nullptr)
    {
        content = std::make_unique<juce::GenericAudioProcessorEditor>(*plugin);
        subtitle = "no interface";
    }

    auto window = std::make_unique<BlockWindow>(block->getDisplayName(), subtitle, category,
                                                std::move(content));
    window->blockUid = uid;
    window->setSize(width, height);

    auto* windowPtr = window.get();
    window->onClose = [this, windowPtr] { closeWindow(windowPtr); };
    window->onTogglePin = [this, windowPtr] { togglePin(windowPtr); };

    // Opens modestly sized near the middle, not filling the app.
    window->setCentrePosition(mWindowLayer.getWidth() / 2,
                              juce::jmax(window->getHeight() / 2 + 10, mWindowLayer.getHeight() / 2 - 40));

    mWindowLayer.addAndMakeVisible(*windowPtr);
    mWindows.push_back(std::move(window));

    layOutWindows();
}

void MainView::openUtilityWindow(juce::String title, BlockCategory category,
                                 std::unique_ptr<juce::Component> content)
{
    auto window = std::make_unique<BlockWindow>(std::move(title), juce::String(), category,
                                                std::move(content));
    window->setSize(520, 320);

    auto* windowPtr = window.get();
    window->onClose = [this, windowPtr] { closeWindow(windowPtr); };
    window->onTogglePin = [this, windowPtr] { togglePin(windowPtr); };
    window->setCentrePosition(mWindowLayer.getWidth() / 2, mWindowLayer.getHeight() / 2 - 30);

    mWindowLayer.addAndMakeVisible(*windowPtr);
    mWindows.push_back(std::move(window));

    layOutWindows();
}

void MainView::closeWindow(BlockWindow* window)
{
    mWindows.erase(std::remove_if(mWindows.begin(), mWindows.end(),
                                  [window](const auto& held) { return held.get() == window; }),
                   mWindows.end());

    layOutWindows();
    updatePanel();
}

void MainView::closeActiveWindow()
{
    // The topmost unpinned window is the one the X and the backdrop refer to.
    for (int i = static_cast<int>(mWindows.size()); --i >= 0;)
    {
        if (!mWindows[static_cast<size_t>(i)]->isPinned())
        {
            closeWindow(mWindows[static_cast<size_t>(i)].get());
            return;
        }
    }
}

void MainView::togglePin(BlockWindow* window)
{
    if (window == nullptr)
        return;

    if (!window->isPinned())
    {
        int pinned = 0;
        for (const auto& held : mWindows)
            if (held->isPinned())
                ++pinned;

        if (pinned >= kMaxPinnedWindows)
        {
            juce::NativeMessageBox::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::InfoIcon)
                    .withTitle("Canvas is full")
                    .withMessage("Up to " + juce::String(kMaxPinnedWindows)
                                 + " windows can be pinned at once. Unpin one to make room.")
                    .withButton("OK"),
                nullptr);
            return;
        }
    }

    window->setPinned(!window->isPinned());
    layOutWindows();
    updatePanel();
}

void MainView::layOutWindows()
{
    // Pinned windows tile the canvas below the lane; unpinned ones float where
    // the user left them.
    const auto canvas = mWindowLayer.getLocalBounds().withTrimmedTop(mLane.getBottom()
                                                                     - mWindowLayer.getY()
                                                                     + theme::metrics::gap);

    std::vector<BlockWindow*> pinned;
    for (const auto& window : mWindows)
        if (window->isPinned())
            pinned.push_back(window.get());

    if (!pinned.empty() && canvas.getHeight() > 80)
    {
        const int columns = pinned.size() <= 2 ? static_cast<int>(pinned.size()) : 3;
        const int rows = (static_cast<int>(pinned.size()) + columns - 1) / columns;
        const int cellWidth = canvas.getWidth() / juce::jmax(1, columns);
        const int cellHeight = canvas.getHeight() / juce::jmax(1, rows);

        for (size_t i = 0; i < pinned.size(); ++i)
        {
            const int column = static_cast<int>(i) % columns;
            const int row = static_cast<int>(i) / columns;

            pinned[i]->setBounds(juce::Rectangle<int>(canvas.getX() + column * cellWidth,
                                                     canvas.getY() + row * cellHeight, cellWidth,
                                                     cellHeight)
                                     .reduced(theme::metrics::gap / 2));
        }
    }

    const bool modal = mWindowLayer.hasModalWindow();
    mWindowLayer.setInterceptsMouseClicks(modal, true);
    mCloseOverlayButton.setVisible(modal);
    mWindowLayer.repaint();
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

void MainView::rigWasRestored()
{
    mLane.refresh();
    updatePanel();
    refreshHeader();
    resized();
}

void MainView::showRigMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "New rig");
    menu.addSeparator();
    menu.addItem(2, "Open rig...");
    menu.addItem(3, "Save rig", mCurrentRigFile != juce::File{});
    menu.addItem(4, "Save rig as...");
    menu.addSeparator();
    menu.addItem(juce::PopupMenu::Item("Rigs are saved to " + rigfiles::getDefaultDirectory().getFileName())
                     .setEnabled(false));

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&mRigButton), [this](int choice) {
        switch (choice)
        {
            case 1:
                mProcessor.getChain().clear();
                mCurrentRigFile = juce::File{};
                mRigName.setText("Untitled rig", juce::dontSendNotification);
                mLane.refresh();
                updatePanel();
                resized();
                break;
            case 2: openRig(); break;
            case 3: saveRig(false); break;
            case 4: saveRig(true); break;
            default: break;
        }
    });
}

void MainView::saveRig(bool forceChooser)
{
    if (!forceChooser && mCurrentRigFile != juce::File{})
    {
        juce::String error;

        if (!rigfiles::save(mProcessor, mCurrentRigFile, error))
            juce::NativeMessageBox::showAsync(juce::MessageBoxOptions()
                                                  .withIconType(juce::MessageBoxIconType::WarningIcon)
                                                  .withTitle("Could not save rig")
                                                  .withMessage(error)
                                                  .withButton("OK"),
                                              nullptr);
        return;
    }

    mFileChooser = std::make_unique<juce::FileChooser>("Save rig", rigfiles::getDefaultDirectory(),
                                                      rigfiles::kFileWildcard);

    mFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this](const juce::FileChooser& chooser) {
                                  const auto file = chooser.getResult();
                                  if (file == juce::File{})
                                      return;

                                  juce::String error;

                                  if (rigfiles::save(mProcessor, file, error))
                                  {
                                      mCurrentRigFile = file.withFileExtension(rigfiles::kFileExtension);
                                      mRigName.setText(mCurrentRigFile.getFileNameWithoutExtension(),
                                                       juce::dontSendNotification);
                                  }
                                  else
                                  {
                                      juce::NativeMessageBox::showAsync(
                                          juce::MessageBoxOptions()
                                              .withIconType(juce::MessageBoxIconType::WarningIcon)
                                              .withTitle("Could not save rig")
                                              .withMessage(error)
                                              .withButton("OK"),
                                          nullptr);
                                  }
                              });
}

void MainView::openRig()
{
    mFileChooser = std::make_unique<juce::FileChooser>("Open rig", rigfiles::getDefaultDirectory(),
                                                      rigfiles::kFileWildcard);

    mFileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& chooser) {
                                  const auto file = chooser.getResult();
                                  if (!file.existsAsFile())
                                      return;

                                  rigfiles::load(mProcessor, file,
                                                 [this, file](rigstate::RestoreResult result,
                                                              juce::String error) {
                                                     if (error.isEmpty())
                                                     {
                                                         mCurrentRigFile = file;
                                                         mRigName.setText(file.getFileNameWithoutExtension(),
                                                                          juce::dontSendNotification);
                                                     }

                                                     mLane.refresh();
                                                     updatePanel();
                                                     resized();
                                                     reportRestore(result, error);
                                                 });
                              });
}

void MainView::reportRestore(const rigstate::RestoreResult& result, const juce::String& error)
{
    juce::String message = error;

    // A rig that half-loaded is worth saying out loud: silence would leave the
    // user wondering why their chain is short.
    if (message.isEmpty() && !result.missingPlugins.isEmpty())
        message = "These plug-ins are not installed and were left as empty slots:\n\n"
                  + result.missingPlugins.joinIntoString("\n");

    if (message.isEmpty() && !result.stateNotRestored.isEmpty())
        message = "These plug-ins loaded but refused their saved settings:\n\n"
                  + result.stateNotRestored.joinIntoString("\n");

    if (message.isEmpty())
        return;

    juce::NativeMessageBox::showAsync(juce::MessageBoxOptions()
                                          .withIconType(juce::MessageBoxIconType::WarningIcon)
                                          .withTitle("Rig loaded with problems")
                                          .withMessage(message)
                                          .withButton("OK"),
                                      nullptr);
}

void MainView::closeAllWindows()
{
    while (!mWindows.empty())
        closeWindow(mWindows.back().get());
}

void MainView::showIoPanel(EndBlock::Kind kind)
{
    openUtilityWindow(kind == EndBlock::Kind::input ? "Input" : "Output", BlockCategory::utility,
                      std::make_unique<IoPanel>(mProcessor, kind, mDeviceManager));
}

int MainView::firstSplitStage() const
{
    const auto& chain = mProcessor.getChain();

    for (int stage = 0; stage < chain.getNumStages(); ++stage)
        if (chain.isStageSplit(stage))
            return stage;

    return -1;
}

void MainView::showSettings()
{
    juce::PopupMenu menu;

    if (mDeviceManager != nullptr)
    {
        menu.addSectionHeader("Audio");
        menu.addItem(10, "Audio device, inputs and outputs...");
    }

    menu.addSectionHeader("Signal");
    menu.addItem(11, "Input...");
    menu.addItem(12, "Output...");

    const auto splitStage = firstSplitStage();
    menu.addItem(13, "Split A / B...", splitStage >= 0);
    menu.addSeparator();

    menu.addItem(1, "Rescan plug-ins...");
    menu.addItem(3, "Close all windows", !mWindows.empty());
    menu.addSeparator();

    const auto denylisted = mProcessor.getCatalog().getDenylist().size();
    menu.addItem(4, "Clear denylist (" + juce::String(denylisted) + ")", denylisted > 0);
    menu.addSeparator();
    menu.addItem(juce::PopupMenu::Item(juce::String(mProcessor.getCatalog().getKnownPluginList().getNumTypes())
                                       + " plug-ins available")
                     .setEnabled(false));

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&mSettingsButton),
                       [this, splitStage](int choice) {
        switch (choice)
        {
            case 1: startScan(); break;
            case 3: closeAllWindows(); break;
            case 10:
                if (mDeviceManager != nullptr)
                    openUtilityWindow("Audio device", BlockCategory::utility,
                                      std::make_unique<AudioSettingsPanel>(*mDeviceManager));
                break;
            case 11: showIoPanel(EndBlock::Kind::input); break;
            case 12: showIoPanel(EndBlock::Kind::output); break;
            case 13:
                if (splitStage >= 0)
                    openUtilityWindow("Split A / B", BlockCategory::utility,
                                      std::make_unique<SplitPanel>(mProcessor, splitStage));
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

    // Header and footer rules.
    g.setColour(theme::colours::outline);
    g.drawHorizontalLine(theme::metrics::headerHeight, 0.0f, static_cast<float>(getWidth()));
    g.drawHorizontalLine(getHeight() - theme::metrics::footerHeight - theme::metrics::gap,
                         static_cast<float>(theme::metrics::padding),
                         static_cast<float>(getWidth() - theme::metrics::padding));
}

void MainView::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop(theme::metrics::headerHeight).reduced(theme::metrics::padding, 0);
    mTitle.setBounds(header.removeFromLeft(104).withSizeKeepingCentre(104, 24));
    mRigButton.setBounds(header.removeFromLeft(52).withSizeKeepingCentre(52, 26));
    header.removeFromLeft(6);
    mRigName.setBounds(header.removeFromLeft(130).withSizeKeepingCentre(130, 24));
    header.removeFromLeft(theme::metrics::gap);

    mMuteButton.setBounds(header.removeFromLeft(178).withSizeKeepingCentre(178, 28));

    mSettingsButton.setBounds(header.removeFromRight(88).withSizeKeepingCentre(88, 26));
    header.removeFromRight(theme::metrics::gap);
    mPluginCountButton.setBounds(header.removeFromRight(110).withSizeKeepingCentre(110, 26));
    header.removeFromRight(theme::metrics::gap);
    mHeaderMeters.setBounds(header.removeFromRight(170).withSizeKeepingCentre(170, 42));
    header.removeFromRight(theme::metrics::gap);
    // Tempo belongs at the top, next to the other things you set rather than watch.
    mTransportBar.setBounds(header.removeFromRight(210).withSizeKeepingCentre(210, 38));

    area.removeFromTop(theme::metrics::gap);
    area = area.reduced(theme::metrics::padding, 0);

    // Footer: load, which you glance at rather than reach for.
    auto footer = area.removeFromBottom(theme::metrics::footerHeight);
    mCpuMeter.setBounds(footer.removeFromRight(126).withSizeKeepingCentre(126, 28));
    area.removeFromBottom(theme::metrics::gap);

    mLane.setBounds(area.removeFromTop(mLane.getPreferredHeight()));
    area.removeFromTop(theme::metrics::gap);

    // Everything under the header belongs to the window layer, so the dim covers
    // the rig but never the header.
    mWindowLayer.setBounds(getLocalBounds().withTrimmedTop(theme::metrics::headerHeight));
    mCanvasHint.setBounds(area);

    mCloseOverlayButton.setBounds(theme::metrics::padding, theme::metrics::headerHeight
                                                               + theme::metrics::gap,
                                  32, 32);

    layOutWindows();
}

} // namespace blockrig
