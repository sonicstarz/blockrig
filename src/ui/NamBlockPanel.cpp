#include "ui/NamBlockPanel.h"

#include "ui/Theme.h"

namespace blockrig
{
namespace
{
void styleKnob(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 15);
}

void styleCaption(juce::Label& label, const juce::String& text)
{
    label.setText(text.toUpperCase(), juce::dontSendNotification);
    // Left-aligned in its cell, as in a parameter grid rather than centred under
    // a floating knob.
    label.setJustificationType(juce::Justification::centredLeft);
    label.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, theme::colours::textFaint);
}
} // namespace

NamBlockPanel::NamBlockPanel(NamBlockProcessor& processor)
    : mProcessor(processor)
{
    mCaptureName.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    mCaptureName.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(mCaptureName);

    mCaptureDetails.setFont(juce::FontOptions(11.0f));
    mCaptureDetails.setColour(juce::Label::textColourId, theme::colours::textFaint);
    mCaptureDetails.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(mCaptureDetails);

    mLoadButton.onClick = [this] { chooseCapture(); };
    addAndMakeVisible(mLoadButton);

    mClearButton.onClick = [this] { mProcessor.clearModel(); };
    addAndMakeVisible(mClearButton);

    addKnob(mInTrim, mInTrimLabel, "Input", "in_trim", mInTrimAtt);
    addKnob(mBass, mBassLabel, "Bass", "bass", mBassAtt);
    addKnob(mMid, mMidLabel, "Mid", "mid", mMidAtt);
    addKnob(mTreble, mTrebleLabel, "Treble", "treble", mTrebleAtt);
    addKnob(mOutTrim, mOutTrimLabel, "Output", "out_trim", mOutTrimAtt);
    addKnob(mGateThreshold, mGateThresholdLabel, "Gate", "gate_thresh", mGateThresholdAtt);
    addKnob(mCalDbu, mCalDbuLabel, "Interface", "cal_dbu", mCalDbuAtt);
    addKnob(mSlim, mSlimLabel, "Size", "slim", mSlimAtt);

    auto& apvts = mProcessor.getValueTreeState();

    addAndMakeVisible(mEqOn);
    mEqOnAtt = std::make_unique<ButtonAttachment>(apvts, "eq_on", mEqOn);

    addAndMakeVisible(mGateOn);
    mGateOnAtt = std::make_unique<ButtonAttachment>(apvts, "gate_on", mGateOn);

    addAndMakeVisible(mCalibrateInput);
    mCalibrateInputAtt = std::make_unique<ButtonAttachment>(apvts, "cal_in", mCalibrateInput);

    mStereo.setTooltip("Runs the capture twice, once per channel, so a stereo signal stays stereo through "
                       "the amp. Costs a second model instance.");
    addAndMakeVisible(mStereo);
    mStereoAtt = std::make_unique<ButtonAttachment>(apvts, "stereo", mStereo);

    styleCaption(mOutputModeLabel, "Output mode");
    addAndMakeVisible(mOutputModeLabel);
    mOutputMode.addItemList({"Raw", "Normalized", "Calibrated"}, 1);
    addAndMakeVisible(mOutputMode);
    mOutputModeAtt = std::make_unique<ComboAttachment>(apvts, "out_mode", mOutputMode);

    mProcessor.onModelStateChanged = [this] { refreshCaptureInfo(); };

    refreshCaptureInfo();
    startTimerHz(15);
}

NamBlockPanel::~NamBlockPanel()
{
    stopTimer();
    mProcessor.onModelStateChanged = nullptr;
}

void NamBlockPanel::addKnob(juce::Slider& slider, juce::Label& label, const juce::String& caption,
                            const char* paramId, std::unique_ptr<SliderAttachment>& attachment)
{
    styleKnob(slider);
    addAndMakeVisible(slider);
    styleCaption(label, caption);
    addAndMakeVisible(label);
    attachment = std::make_unique<SliderAttachment>(mProcessor.getValueTreeState(), paramId, slider);
}

void NamBlockPanel::chooseCapture()
{
    mFileChooser = std::make_unique<juce::FileChooser>("Load a NAM capture", juce::File{}, "*.nam");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    mFileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser) {
        const auto file = chooser.getResult();
        if (file.existsAsFile())
            mProcessor.loadModel(file);
    });
}

void NamBlockPanel::refreshCaptureInfo()
{
    const auto info = mProcessor.getModelInfo();
    const auto error = mProcessor.getModelError();

    if (error.isNotEmpty())
    {
        mCaptureName.setText("Could not load capture", juce::dontSendNotification);
        mCaptureName.setColour(juce::Label::textColourId, theme::colours::bad);
        mCaptureDetails.setText(error, juce::dontSendNotification);
    }
    else if (info.json.isNotEmpty())
    {
        mCaptureName.setText(info.name, juce::dontSendNotification);
        mCaptureName.setColour(juce::Label::textColourId, theme::colours::text);

        juce::StringArray details;
        details.add(juce::String(juce::roundToInt(info.metrics.modelSampleRate / 1000.0)) + " kHz");
        if (info.metrics.resampling)
            details.add("resampled, " + juce::String(info.metrics.latencySamples) + " smp latency");
        if (info.metrics.hasLoudness)
            details.add("loudness " + juce::String(info.metrics.loudness, 1) + " dB");
        if (info.metrics.hasInputLevel)
            details.add("in " + juce::String(info.metrics.inputLevel, 1) + " dBu");
        if (info.metrics.hasOutputLevel)
            details.add("out " + juce::String(info.metrics.outputLevel, 1) + " dBu");
        if (info.metrics.slimmable)
            details.add("slimmable (A2)");

        mCaptureDetails.setText(details.joinIntoString("   •   "), juce::dontSendNotification);
    }
    else
    {
        mCaptureName.setText("No capture loaded", juce::dontSendNotification);
        mCaptureName.setColour(juce::Label::textColourId, theme::colours::textFaint);
        mCaptureDetails.setText("Drop a .nam file here, or use Load capture", juce::dontSendNotification);
    }

    // Only offer what this particular capture supports.
    const bool loaded = info.json.isNotEmpty();
    mSlim.setEnabled(loaded && info.metrics.slimmable);
    mSlimLabel.setEnabled(loaded && info.metrics.slimmable);
    mCalibrateInput.setEnabled(loaded && info.metrics.hasInputLevel);
    mCalDbu.setEnabled(loaded && (info.metrics.hasInputLevel || info.metrics.hasOutputLevel));
    mClearButton.setEnabled(loaded);

    repaint();
}

void NamBlockPanel::timerCallback()
{
    const auto input = mProcessor.getInputLevel();
    const auto output = mProcessor.getOutputLevel();

    if (std::abs(input - mInputLevel) > 0.01f || std::abs(output - mOutputLevel) > 0.01f)
    {
        mInputLevel = input;
        mOutputLevel = output;
        repaint();
    }
}

bool NamBlockPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& file : files)
        if (file.endsWithIgnoreCase(".nam"))
            return true;
    return false;
}

void NamBlockPanel::fileDragEnter(const juce::StringArray&, int, int)
{
    mDragHighlight = true;
    repaint();
}

void NamBlockPanel::fileDragExit(const juce::StringArray&)
{
    mDragHighlight = false;
    repaint();
}

void NamBlockPanel::filesDropped(const juce::StringArray& files, int, int)
{
    mDragHighlight = false;
    repaint();

    for (const auto& file : files)
    {
        if (file.endsWithIgnoreCase(".nam"))
        {
            mProcessor.loadModel(juce::File(file));
            break;
        }
    }
}

void NamBlockPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);

    g.setColour(theme::colours::panel);
    g.fillRoundedRectangle(bounds, theme::metrics::cornerRadius);
    g.setColour(mDragHighlight ? theme::colours::accent : theme::colours::outline);
    g.drawRoundedRectangle(bounds, theme::metrics::cornerRadius, mDragHighlight ? 2.0f : 1.0f);

    // Cell dividers behind the knob row, so parameters read as a grid rather
    // than as controls floating in a panel.
    if (!mKnobCells.isEmpty())
    {
        g.setColour(theme::colours::outline.withAlpha(0.6f));

        for (int i = 1; i < mKnobCells.size(); ++i)
        {
            const auto cell = mKnobCells[i];
            g.fillRect(static_cast<float>(cell.getX()) - 0.5f, static_cast<float>(cell.getY()) + 4.0f, 1.0f,
                       static_cast<float>(cell.getHeight()) - 8.0f);
        }
    }

    // In/out meters down the right edge, so the amp's own levels are visible
    // without hunting.
    auto meters = bounds.reduced(theme::metrics::padding).removeFromRight(10.0f);
    const auto meterHeight = meters.getHeight() * 0.42f;
    theme::drawLevelMeter(g, meters.removeFromTop(meterHeight), mInputLevel, true);
    meters.removeFromTop(6.0f);
    theme::drawLevelMeter(g, meters.removeFromTop(meterHeight), mOutputLevel, true);
}

void NamBlockPanel::resized()
{
    auto area = getLocalBounds().reduced(theme::metrics::padding);
    area.removeFromRight(20); // meters

    auto header = area.removeFromTop(46);
    auto buttons = header.removeFromRight(200);
    mLoadButton.setBounds(buttons.removeFromLeft(110).reduced(2, 10));
    mClearButton.setBounds(buttons.reduced(2, 10));

    mCaptureName.setBounds(header.removeFromTop(24));
    mCaptureDetails.setBounds(header);

    area.removeFromTop(theme::metrics::gap);

    // Knob row: the controls people actually reach for.
    auto knobs = area.removeFromTop(84);
    mKnobCells.clearQuick();

    const auto layoutKnob = [this, &knobs](juce::Slider& slider, juce::Label& label, int width) {
        auto cell = knobs.removeFromLeft(width);
        mKnobCells.add(cell);
        label.setBounds(cell.removeFromTop(15));
        slider.setBounds(cell);
    };

    const int knobWidth = juce::jmax(64, knobs.getWidth() / 8);
    layoutKnob(mInTrim, mInTrimLabel, knobWidth);
    layoutKnob(mBass, mBassLabel, knobWidth);
    layoutKnob(mMid, mMidLabel, knobWidth);
    layoutKnob(mTreble, mTrebleLabel, knobWidth);
    layoutKnob(mOutTrim, mOutTrimLabel, knobWidth);
    layoutKnob(mGateThreshold, mGateThresholdLabel, knobWidth);
    layoutKnob(mCalDbu, mCalDbuLabel, knobWidth);
    layoutKnob(mSlim, mSlimLabel, knobWidth);

    area.removeFromTop(theme::metrics::gap);

    auto toggles = area.removeFromTop(26);
    mEqOn.setBounds(toggles.removeFromLeft(64));
    mGateOn.setBounds(toggles.removeFromLeft(74));
    mCalibrateInput.setBounds(toggles.removeFromLeft(138));
    mStereo.setBounds(toggles.removeFromLeft(116));

    auto mode = toggles.removeFromRight(240);
    mOutputModeLabel.setBounds(mode.removeFromLeft(90));
    mOutputMode.setBounds(mode.reduced(4, 1));
}

} // namespace blockrig
