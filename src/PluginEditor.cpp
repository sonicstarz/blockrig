#include "PluginEditor.h"

namespace nammodeler
{
namespace
{
constexpr int kPanelWidth = 380;
constexpr int kUtilityHeight = 120;
constexpr int kEditorWidth = kPanelWidth * 2 + 30;
constexpr int kEditorHeight = 560;

void styleRotary(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 68, 18);
}

void styleCaption(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(12.0f));
}
} // namespace

AmpSlotPanel::AmpSlotPanel(NAMModelerProcessor& processor, int slotIndex)
    : mProcessor(processor)
    , mSlotIndex(slotIndex)
{
    mTitle.setText(slotIndex == 0 ? "AMP A" : "AMP B", juce::dontSendNotification);
    mTitle.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    mTitle.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(mTitle);

    mModelName.setText("No model loaded", juce::dontSendNotification);
    mModelName.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(mModelName);

    mModelDetails.setFont(juce::FontOptions(11.0f));
    mModelDetails.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(mModelDetails);

    mLoadButton.onClick = [this] { chooseModel(); };
    addAndMakeVisible(mLoadButton);

    mClearButton.onClick = [this] { mProcessor.clearModel(mSlotIndex); };
    mClearButton.setTooltip("Unload this model");
    addAndMakeVisible(mClearButton);

    attachButton(mEnabled, pid::enabled, mEnabledAttachment);
    attachButton(mPhase, pid::phase, mPhaseAttachment);
    mPhase.setTooltip("Invert polarity");
    attachButton(mSolo, pid::solo, mSoloAttachment);
    attachButton(mMute, pid::mute, mMuteAttachment);
    attachButton(mEqOn, pid::eqOn, mEqOnAttachment);
    attachButton(mCalibrateInput, pid::calIn, mCalibrateInputAttachment);

    attachSlider(mInTrim, mInTrimLabel, "Input", pid::inTrim, mInTrimAttachment);
    attachSlider(mOutTrim, mOutTrimLabel, "Output", pid::outTrim, mOutTrimAttachment);
    attachSlider(mPan, mPanLabel, "Pan", pid::pan, mPanAttachment);
    attachSlider(mBass, mBassLabel, "Bass", pid::bass, mBassAttachment);
    attachSlider(mMid, mMidLabel, "Mid", pid::mid, mMidAttachment);
    attachSlider(mTreble, mTrebleLabel, "Treble", pid::treble, mTrebleAttachment);
    attachSlider(mCalDbu, mCalDbuLabel, "Interface", pid::calDbu, mCalDbuAttachment);
    attachSlider(mSlim, mSlimLabel, "Model Size", pid::slim, mSlimAttachment);

    styleCaption(mOutputModeLabel, "Output Mode");
    addAndMakeVisible(mOutputModeLabel);
    mOutputMode.addItemList({"Raw", "Normalized", "Calibrated"}, 1);
    addAndMakeVisible(mOutputMode);
    mOutputModeAttachment = std::make_unique<ComboAttachment>(mProcessor.getValueTreeState(),
                                                             pid::slotParam(mSlotIndex, pid::outMode), mOutputMode);

    refreshModelState();
}

void AmpSlotPanel::attachSlider(juce::Slider& slider, juce::Label& label, const juce::String& text,
                                const char* paramId, std::unique_ptr<SliderAttachment>& attachment)
{
    styleRotary(slider);
    addAndMakeVisible(slider);
    styleCaption(label, text);
    addAndMakeVisible(label);
    attachment = std::make_unique<SliderAttachment>(mProcessor.getValueTreeState(),
                                                    pid::slotParam(mSlotIndex, paramId), slider);
}

void AmpSlotPanel::attachButton(juce::Button& button, const char* paramId,
                                std::unique_ptr<ButtonAttachment>& attachment)
{
    addAndMakeVisible(button);
    attachment = std::make_unique<ButtonAttachment>(mProcessor.getValueTreeState(),
                                                    pid::slotParam(mSlotIndex, paramId), button);
}

void AmpSlotPanel::chooseModel()
{
    mFileChooser = std::make_unique<juce::FileChooser>("Load a NAM capture", juce::File{}, "*.nam");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    mFileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser) {
        const auto file = chooser.getResult();
        if (file.existsAsFile())
            mProcessor.loadModel(mSlotIndex, file);
    });
}

bool AmpSlotPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& file : files)
        if (file.endsWithIgnoreCase(".nam"))
            return true;
    return false;
}

void AmpSlotPanel::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& file : files)
    {
        if (file.endsWithIgnoreCase(".nam"))
        {
            mProcessor.loadModel(mSlotIndex, juce::File(file));
            break;
        }
    }
    mDragHighlight = false;
    repaint();
}

void AmpSlotPanel::refreshModelState()
{
    const auto info = mProcessor.getModelInfo(mSlotIndex);
    const auto error = mProcessor.getSlotError(mSlotIndex);

    if (error.isNotEmpty())
    {
        mModelName.setText("Load failed", juce::dontSendNotification);
        mModelName.setColour(juce::Label::textColourId, juce::Colours::orangered);
        mModelDetails.setText(error, juce::dontSendNotification);
    }
    else if (info.json.isNotEmpty())
    {
        mModelName.setText(info.name, juce::dontSendNotification);
        mModelName.setColour(juce::Label::textColourId, juce::Colours::white);

        juce::StringArray details;
        details.add(juce::String(juce::roundToInt(info.metrics.modelSampleRate / 1000.0)) + " kHz");
        if (info.metrics.resampling)
            details.add("resampling (" + juce::String(info.metrics.latencySamples) + " smp)");
        if (info.metrics.hasLoudness)
            details.add("loudness " + juce::String(info.metrics.loudness, 1) + " dB");
        if (info.metrics.hasInputLevel)
            details.add("in " + juce::String(info.metrics.inputLevel, 1) + " dBu");
        if (info.metrics.hasOutputLevel)
            details.add("out " + juce::String(info.metrics.outputLevel, 1) + " dBu");
        if (info.metrics.slimmable)
            details.add("slimmable");

        mModelDetails.setText(details.joinIntoString("  •  "), juce::dontSendNotification);
    }
    else
    {
        mModelName.setText("No model loaded", juce::dontSendNotification);
        mModelName.setColour(juce::Label::textColourId, juce::Colours::grey);
        mModelDetails.setText("Drop a .nam file here", juce::dontSendNotification);
    }

    // Only offer what this particular model actually supports.
    const bool loaded = info.json.isNotEmpty();
    mSlim.setEnabled(loaded && info.metrics.slimmable);
    mSlimLabel.setEnabled(loaded && info.metrics.slimmable);
    mCalibrateInput.setEnabled(loaded && info.metrics.hasInputLevel);
    mOutputMode.setEnabled(loaded);

    if (loaded)
    {
        // Normalized needs loudness; Calibrated needs an output level.
        if (auto* item = mOutputMode.getRootMenu())
        {
            juce::PopupMenu::MenuItemIterator iterator(*item);
            while (iterator.next())
            {
                auto& entry = iterator.getItem();
                if (entry.itemID == 2)
                    entry.isEnabled = info.metrics.hasLoudness;
                else if (entry.itemID == 3)
                    entry.isEnabled = info.metrics.hasOutputLevel;
            }
        }
    }

    repaint();
}

void AmpSlotPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);

    g.setColour(juce::Colour(0xff23262b));
    g.fillRoundedRectangle(bounds, 6.0f);

    g.setColour(mDragHighlight ? juce::Colours::orange : juce::Colour(0xff3a3f46));
    g.drawRoundedRectangle(bounds, 6.0f, mDragHighlight ? 2.0f : 1.0f);
}

void AmpSlotPanel::resized()
{
    auto area = getLocalBounds().reduced(12);

    auto header = area.removeFromTop(26);
    mTitle.setBounds(header.removeFromLeft(90));
    mClearButton.setBounds(header.removeFromRight(30));
    mLoadButton.setBounds(header.removeFromRight(90));

    mModelName.setBounds(area.removeFromTop(22));
    mModelDetails.setBounds(area.removeFromTop(18));
    area.removeFromTop(8);

    auto toggles = area.removeFromTop(24);
    const int toggleWidth = toggles.getWidth() / 4;
    mEnabled.setBounds(toggles.removeFromLeft(toggleWidth));
    mPhase.setBounds(toggles.removeFromLeft(toggleWidth));
    mSolo.setBounds(toggles.removeFromLeft(toggleWidth));
    mMute.setBounds(toggles);
    area.removeFromTop(10);

    // Trims and pan.
    const auto layoutRow = [&area](std::initializer_list<std::pair<juce::Slider*, juce::Label*>> controls) {
        auto row = area.removeFromTop(96);
        const int width = row.getWidth() / static_cast<int>(controls.size());
        for (auto& [slider, label] : controls)
        {
            auto cell = row.removeFromLeft(width);
            label->setBounds(cell.removeFromTop(16));
            slider->setBounds(cell);
        }
    };

    layoutRow({{&mInTrim, &mInTrimLabel}, {&mOutTrim, &mOutTrimLabel}, {&mPan, &mPanLabel}});

    area.removeFromTop(6);
    auto eqHeader = area.removeFromTop(22);
    mEqOn.setBounds(eqHeader.removeFromLeft(70));

    layoutRow({{&mBass, &mBassLabel}, {&mMid, &mMidLabel}, {&mTreble, &mTrebleLabel}});

    area.removeFromTop(10);
    auto modeRow = area.removeFromTop(44);
    auto modeLeft = modeRow.removeFromLeft(modeRow.getWidth() / 2);
    mOutputModeLabel.setBounds(modeLeft.removeFromTop(16));
    mOutputMode.setBounds(modeLeft.reduced(4, 2));
    mCalibrateInput.setBounds(modeRow.reduced(4, 10));

    layoutRow({{&mCalDbu, &mCalDbuLabel}, {&mSlim, &mSlimLabel}});
}

NAMModelerEditor::NAMModelerEditor(NAMModelerProcessor& processor)
    : juce::AudioProcessorEditor(&processor)
    , mProcessor(processor)
{
    for (int i = 0; i < NAMModelerProcessor::kNumSlots; ++i)
    {
        mSlotPanels[static_cast<size_t>(i)] = std::make_unique<AmpSlotPanel>(mProcessor, i);
        addAndMakeVisible(*mSlotPanels[static_cast<size_t>(i)]);
    }

    styleCaption(mInputModeLabel, "Input");
    addAndMakeVisible(mInputModeLabel);
    mInputMode.addItemList({"Mono", "Stereo"}, 1);
    addAndMakeVisible(mInputMode);
    mInputModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        mProcessor.getValueTreeState(), pid::inputMode, mInputMode);

    styleCaption(mMasterOutLabel, "Master");
    addAndMakeVisible(mMasterOutLabel);
    styleRotary(mMasterOut);
    addAndMakeVisible(mMasterOut);
    mMasterOutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        mProcessor.getValueTreeState(), pid::masterOut, mMasterOut);

    addAndMakeVisible(mMonoSum);
    mMonoSumAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        mProcessor.getValueTreeState(), pid::monoSum, mMonoSum);

    addAndMakeVisible(mGateOn);
    mGateOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        mProcessor.getValueTreeState(), pid::gateOn, mGateOn);

    styleCaption(mGateThresholdLabel, "Gate Thresh");
    addAndMakeVisible(mGateThresholdLabel);
    styleRotary(mGateThreshold);
    addAndMakeVisible(mGateThreshold);
    mGateThresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        mProcessor.getValueTreeState(), pid::gateThresh, mGateThreshold);

    mLatencyLabel.setFont(juce::FontOptions(11.0f));
    mLatencyLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(mLatencyLabel);

    mProcessor.onModelStateChanged = [this] {
        for (auto& panel : mSlotPanels)
            if (panel != nullptr)
                panel->refreshModelState();
    };

    setSize(kEditorWidth, kEditorHeight);
    startTimerHz(15);
}

NAMModelerEditor::~NAMModelerEditor()
{
    mProcessor.onModelStateChanged = nullptr;
}

void NAMModelerEditor::timerCallback()
{
    const int latency = mProcessor.getLatencySamples();
    const double sampleRate = mProcessor.getSampleRate();
    juce::String text = "Latency: " + juce::String(latency) + " samples";
    if (latency > 0 && sampleRate > 0.0)
        text += " (" + juce::String(1000.0 * latency / sampleRate, 2) + " ms)";
    mLatencyLabel.setText(text, juce::dontSendNotification);
}

void NAMModelerEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1c20));
}

void NAMModelerEditor::resized()
{
    auto area = getLocalBounds().reduced(8);

    auto utility = area.removeFromBottom(kUtilityHeight);

    auto panels = area;
    const int panelWidth = panels.getWidth() / 2;
    mSlotPanels[0]->setBounds(panels.removeFromLeft(panelWidth).reduced(4));
    mSlotPanels[1]->setBounds(panels.reduced(4));

    utility = utility.reduced(8);
    mLatencyLabel.setBounds(utility.removeFromBottom(16));

    auto inputArea = utility.removeFromLeft(140);
    mInputModeLabel.setBounds(inputArea.removeFromTop(16));
    mInputMode.setBounds(inputArea.removeFromTop(26).reduced(4, 0));

    auto gateArea = utility.removeFromLeft(160);
    mGateOn.setBounds(gateArea.removeFromTop(24));
    mGateThresholdLabel.setBounds(gateArea.removeFromTop(16));
    mGateThreshold.setBounds(gateArea);

    auto masterArea = utility.removeFromRight(160);
    mMasterOutLabel.setBounds(masterArea.removeFromTop(16));
    mMasterOut.setBounds(masterArea.removeFromTop(60));

    mMonoSum.setBounds(utility.removeFromRight(120).withHeight(24));
}

} // namespace nammodeler
