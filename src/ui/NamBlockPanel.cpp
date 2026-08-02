#include "ui/NamBlockPanel.h"

#include <map>

#include "ui/BlockCategories.h"

namespace blockrig
{

NamBlockPanel::NamBlockPanel(NamBlockProcessor& processor)
    : mProcessor(processor)
{
    mLibraryButton.setTooltip("Every capture you load is collected here automatically.");
    mLibraryButton.onClick = [this] { showLibraryMenu(); };

    mLoadButton.setTooltip("Load a .nam file. It joins the library automatically.");
    mLoadButton.onClick = [this] { chooseCapture(); };

    mClearButton.onClick = [this] { mProcessor.clearModel(); };

    mStereo.setTooltip("Runs the capture twice, once per channel, so a stereo signal stays stereo "
                       "through the amp. Costs a second model instance.");

    mTitleBarRow.add(mLibraryButton, 70);
    mTitleBarRow.add(mLoadButton, 68);
    mTitleBarRow.add(mClearButton, 56);
    mTitleBarRow.addGap(10);
    mTitleBarRow.add(mEqOn, 52);
    mTitleBarRow.add(mGateOn, 62);
    mTitleBarRow.add(mStereo, 76);

    addKnob(mInTrim, mInTrimLabel, "Input", "in_trim", mInTrimAtt);
    addKnob(mBass, mBassLabel, "Bass", "bass", mBassAtt);
    addKnob(mMid, mMidLabel, "Mid", "mid", mMidAtt);
    addKnob(mTreble, mTrebleLabel, "Treble", "treble", mTrebleAtt);
    addKnob(mOutTrim, mOutTrimLabel, "Output", "out_trim", mOutTrimAtt);
    addKnob(mGateThreshold, mGateThresholdLabel, "Gate", "gate_thresh", mGateThresholdAtt);
    addKnob(mCalDbu, mCalDbuLabel, "Interface", "cal_dbu", mCalDbuAtt);
    addKnob(mSlim, mSlimLabel, "Size", "slim", mSlimAtt);

    auto& apvts = mProcessor.getValueTreeState();

    mEqOnAtt = std::make_unique<ButtonAttachment>(apvts, "eq_on", mEqOn);
    mGateOnAtt = std::make_unique<ButtonAttachment>(apvts, "gate_on", mGateOn);
    mStereoAtt = std::make_unique<ButtonAttachment>(apvts, "stereo", mStereo);

    addAndMakeVisible(mCalibrateInput);
    mCalibrateInputAtt = std::make_unique<ButtonAttachment>(apvts, "cal_in", mCalibrateInput);

    theme::editor::styleCaption(mOutputModeLabel, "Output mode");
    mOutputModeLabel.setJustificationType(juce::Justification::centredLeft);
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
    theme::editor::styleKnob(slider, getCategoryColour(BlockCategory::amp));
    addAndMakeVisible(slider);
    theme::editor::styleCaption(label, caption);
    addAndMakeVisible(label);
    attachment = std::make_unique<SliderAttachment>(mProcessor.getValueTreeState(), paramId, slider);
}

void NamBlockPanel::setSubtitle(const juce::String& subtitle)
{
    mSubtitle = subtitle;
    if (onSubtitleChanged)
        onSubtitleChanged(mSubtitle);
}

void NamBlockPanel::showLibraryMenu()
{
    auto& library = mProcessor.getCaptureLibrary();
    const auto entries = library.getEntries();

    juce::PopupMenu menu;

    if (entries.isEmpty())
    {
        menu.addItem(juce::PopupMenu::Item("No captures yet - load a .nam file and it will appear here")
                         .setEnabled(false));
    }
    else
    {
        // Newest first; subfolders of the library folder become submenus, so
        // organising in Finder is organising the menu.
        const auto currentName = mProcessor.getModelInfo().name;

        std::map<juce::String, juce::PopupMenu> folders;
        juce::StringArray folderOrder;
        int id = 100;

        for (const auto& entry : entries)
        {
            const auto item = juce::PopupMenu::Item(entry.name)
                                  .setID(id++)
                                  .setTicked(entry.name == currentName);

            if (entry.folder.isEmpty())
            {
                menu.addItem(item);
            }
            else
            {
                if (folders.find(entry.folder) == folders.end())
                    folderOrder.add(entry.folder);
                folders[entry.folder].addItem(item);
            }
        }

        folderOrder.sortNatural();
        for (const auto& folder : folderOrder)
            menu.addSubMenu(folder, folders[folder]);
    }

    menu.addSeparator();
    menu.addItem(1, "Open file...");
    menu.addItem(2, "Show library folder");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&mLibraryButton),
                       [this, entries](int choice) {
        if (choice == 1)
        {
            chooseCapture();
        }
        else if (choice == 2)
        {
            mProcessor.getCaptureLibrary().getDirectory().revealToUser();
        }
        else if (choice >= 100)
        {
            const auto index = choice - 100;
            if (index < entries.size())
                mProcessor.loadModel(entries[index].file);
        }
    });
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
        setSubtitle("could not load: " + error);
        mDetails.clear();
    }
    else if (info.json.isNotEmpty())
    {
        setSubtitle(info.name);

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

        mDetails = details.joinIntoString(juce::String::fromUTF8("  \xc2\xb7  "));
    }
    else
    {
        setSubtitle("no capture loaded");
        mDetails = "Drop a .nam file here, or use Open...";
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
    // Divider between the knob row and the output-mode column.
    g.setColour(theme::colours::hairline);
    g.fillRect(mDividerX, 10.0f, 1.0f, static_cast<float>(getHeight()) - 20.0f);

    // Gain staging visible while the knobs are being set.
    g.setColour(theme::colours::textFaint);
    g.setFont(theme::fonts::ui(10.0f, 500));
    g.drawText("In", mInMeter.withWidth(24.0f), juce::Justification::centredLeft, false);
    theme::drawLevelMeter(g, mInMeter.withTrimmedLeft(26.0f), mInputLevel);
    g.setColour(theme::colours::textFaint);
    g.drawText("Out", mOutMeter.withWidth(24.0f), juce::Justification::centredLeft, false);
    theme::drawLevelMeter(g, mOutMeter.withTrimmedLeft(26.0f), mOutputLevel);

    // The capture's metadata, faint along the bottom.
    if (mDetails.isNotEmpty())
    {
        g.setColour(theme::colours::textGhost);
        g.setFont(theme::fonts::mono(10.0f));
        g.drawText(mDetails, mDetailsArea, juce::Justification::centredLeft, true);
    }

    if (mDragHighlight)
    {
        g.setColour(getCategoryColour(BlockCategory::amp));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), theme::metrics::radiusMd,
                               2.0f);
    }
}

void NamBlockPanel::resized()
{
    auto area = getLocalBounds().reduced(theme::metrics::padding, 8);

    mDetailsArea = area.removeFromBottom(16);

    auto knobs = area.removeFromLeft(8 * theme::editor::cellWidth)
                     .withSizeKeepingCentre(8 * theme::editor::cellWidth, theme::editor::cellHeight);
    const auto layoutKnob = [&knobs](juce::Slider& slider, juce::Label& label) {
        theme::editor::layoutKnobCell(knobs.removeFromLeft(theme::editor::cellWidth), slider, label);
    };

    layoutKnob(mInTrim, mInTrimLabel);
    layoutKnob(mBass, mBassLabel);
    layoutKnob(mMid, mMidLabel);
    layoutKnob(mTreble, mTrebleLabel);
    layoutKnob(mOutTrim, mOutTrimLabel);
    layoutKnob(mGateThreshold, mGateThresholdLabel);
    layoutKnob(mCalDbu, mCalDbuLabel);
    layoutKnob(mSlim, mSlimLabel);

    area.removeFromLeft(theme::metrics::gap);
    mDividerX = static_cast<float>(area.getX());
    area.removeFromLeft(theme::metrics::gap + 1);

    // The output-mode column keeps the mock's compact width however wide the
    // docked editor stretches.
    const auto columnWidth = juce::jmin(area.getWidth(), 200);
    auto column = area.removeFromLeft(columnWidth).withSizeKeepingCentre(columnWidth, 96);
    mOutputModeLabel.setBounds(column.removeFromTop(16));
    mOutputMode.setBounds(column.removeFromTop(26).reduced(0, 1));
    column.removeFromTop(4);
    mCalibrateInput.setBounds(column.removeFromTop(20));
    column.removeFromTop(6);
    mInMeter = column.removeFromTop(10).toFloat();
    column.removeFromTop(4);
    mOutMeter = column.removeFromTop(10).toFloat();
}

} // namespace blockrig
