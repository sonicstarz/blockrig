#include "ui/NamBlockPanel.h"

#include <map>

#include "ui/BlockCategories.h"
#include "ui/Theme.h"

namespace blockrig
{
namespace
{
void styleKnob(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 66, 16);
    slider.setColour(juce::Slider::textBoxTextColourId, theme::colours::text);
}

void styleCaption(juce::Label& label, const juce::String& text)
{
    label.setText(text.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
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

    mLibraryButton.setTooltip("Every capture you load is collected here automatically.");
    mLibraryButton.onClick = [this] { showLibraryMenu(); };
    addAndMakeVisible(mLibraryButton);

    mLoadButton.setTooltip("Load a .nam file. It joins the library automatically.");
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
    auto bounds = getLocalBounds().toFloat();
    const auto accent = getCategoryColour(BlockCategory::amp);

    g.setColour(theme::colours::panel);
    g.fillRect(bounds);

    // A faceplate behind the controls with a lit top edge. Deliberately generic:
    // a capture carries no artwork, and inventing a specific amp's livery for an
    // arbitrary .nam would be a lie about what has been loaded.
    auto plate = bounds.withTrimmedTop(52.0f).reduced(theme::metrics::gap, theme::metrics::gap * 0.5f);
    g.setColour(theme::colours::background);
    g.fillRoundedRectangle(plate, theme::metrics::cornerRadius);
    g.setColour(accent.withAlpha(0.20f));
    g.drawRoundedRectangle(plate, theme::metrics::cornerRadius, 1.0f);
    g.setColour(accent.withAlpha(0.45f));
    g.fillRect(plate.getX() + 14.0f, plate.getY(), plate.getWidth() - 28.0f, 1.4f);

    // Header: the category glyph, then whatever capture is loaded.
    auto header = bounds.removeFromTop(52.0f).reduced(theme::metrics::gap, 6.0f);
    drawCategoryIcon(g, header.removeFromLeft(30.0f).reduced(2.0f, 7.0f), BlockCategory::amp, accent, 1.6f);

    // Gain staging visible while the knobs are being set.
    auto meters = header.removeFromRight(86.0f).withTrimmedTop(6.0f);
    g.setColour(theme::colours::textFaint);
    g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    g.drawText("IN", meters.removeFromTop(9.0f), juce::Justification::topLeft, false);
    theme::drawLevelMeter(g, meters.removeFromTop(5.0f), mInputLevel);
    meters.removeFromTop(5.0f);
    g.setColour(theme::colours::textFaint);
    g.drawText("OUT", meters.removeFromTop(9.0f), juce::Justification::topLeft, false);
    theme::drawLevelMeter(g, meters.removeFromTop(5.0f), mOutputLevel);

    if (mDragHighlight)
    {
        g.setColour(accent);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), theme::metrics::cornerRadius, 2.0f);
    }
}

void NamBlockPanel::resized()
{
    auto area = getLocalBounds().reduced(theme::metrics::gap, 0);

    auto header = area.removeFromTop(52).withTrimmedTop(6).withTrimmedBottom(6);
    header.removeFromLeft(30);  // glyph
    header.removeFromRight(86); // meters

    auto buttons = header.removeFromRight(232);
    mClearButton.setBounds(buttons.removeFromRight(54).reduced(2, 8));
    mLoadButton.setBounds(buttons.removeFromRight(74).reduced(2, 8));
    mLibraryButton.setBounds(buttons.removeFromRight(84).reduced(2, 8));

    mCaptureName.setBounds(header.removeFromTop(22));
    mCaptureDetails.setBounds(header);

    area.reduce(theme::metrics::gap, 0);
    area.removeFromTop(theme::metrics::gap);

    mKnobCells.clearQuick();

    auto knobs = area.removeFromTop(84);
    const auto layoutKnob = [this, &knobs](juce::Slider& slider, juce::Label& label, int width) {
        auto cell = knobs.removeFromLeft(width);
        mKnobCells.add(cell);
        label.setBounds(cell.removeFromTop(13));
        slider.setBounds(cell);
    };

    const int width = juce::jmax(56, juce::jmin(78, knobs.getWidth() / 8));
    layoutKnob(mInTrim, mInTrimLabel, width);
    layoutKnob(mBass, mBassLabel, width);
    layoutKnob(mMid, mMidLabel, width);
    layoutKnob(mTreble, mTrebleLabel, width);
    layoutKnob(mOutTrim, mOutTrimLabel, width);
    layoutKnob(mGateThreshold, mGateThresholdLabel, width);
    layoutKnob(mCalDbu, mCalDbuLabel, width);
    layoutKnob(mSlim, mSlimLabel, width);

    area.removeFromTop(6);

    auto switches = area.removeFromTop(26);
    auto mode = switches.removeFromRight(180);
    mOutputModeLabel.setBounds(mode.removeFromLeft(52));
    mOutputMode.setBounds(mode.reduced(2, 1));

    mEqOn.setBounds(switches.removeFromLeft(56));
    mGateOn.setBounds(switches.removeFromLeft(66));
    mStereo.setBounds(switches.removeFromLeft(104));
    mCalibrateInput.setBounds(switches.removeFromLeft(124));
}

} // namespace blockrig
