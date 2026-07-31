#include "ui/BuiltInBlockPanel.h"

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

/// Header shared by the built-in panels: category glyph, title, subtitle.
void paintHeader(juce::Graphics& g, juce::Rectangle<float> header, BlockCategory category)
{
    const auto accent = getCategoryColour(category);
    drawCategoryIcon(g, header.removeFromLeft(28.0f).reduced(2.0f, 6.0f), category, accent, 1.6f);
}
} // namespace

//==============================================================================
IrBlockPanel::IrBlockPanel(IrBlockProcessor& processor)
    : mProcessor(processor)
{
    mIrName.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    mIrName.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(mIrName);

    mIrDetails.setFont(juce::FontOptions(11.0f));
    mIrDetails.setColour(juce::Label::textColourId, theme::colours::textFaint);
    mIrDetails.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(mIrDetails);

    mLibraryButton.setTooltip("Every IR you load is collected here automatically.");
    mLibraryButton.onClick = [this] { showLibraryMenu(); };
    addAndMakeVisible(mLibraryButton);

    mLoadButton.setTooltip("Load a .wav or .aiff impulse response.");
    mLoadButton.onClick = [this] { chooseIr(); };
    addAndMakeVisible(mLoadButton);

    mClearButton.onClick = [this] { mProcessor.clearIr(); };
    addAndMakeVisible(mClearButton);

    styleKnob(mMix);
    mMix.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff2ebfa5)); // cab teal
    addAndMakeVisible(mMix);
    styleCaption(mMixLabel, "Mix");
    addAndMakeVisible(mMixLabel);
    mMixAtt = std::make_unique<SliderAttachment>(mProcessor.getValueTreeState(), "mix", mMix);

    styleKnob(mOutput);
    mOutput.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff2ebfa5));
    addAndMakeVisible(mOutput);
    styleCaption(mOutputLabel, "Output");
    addAndMakeVisible(mOutputLabel);
    mOutputAtt = std::make_unique<SliderAttachment>(mProcessor.getValueTreeState(), "out_trim", mOutput);

    mProcessor.onIrChanged = [this] { refresh(); };
    refresh();
}

IrBlockPanel::~IrBlockPanel()
{
    mProcessor.onIrChanged = nullptr;
}

void IrBlockPanel::refresh()
{
    const auto name = mProcessor.getIrName();

    if (name.isNotEmpty())
    {
        mIrName.setText(name, juce::dontSendNotification);
        mIrName.setColour(juce::Label::textColourId, theme::colours::text);
        mIrDetails.setText(mProcessor.getIrFile().getParentDirectory().getFileName(),
                           juce::dontSendNotification);
    }
    else
    {
        mIrName.setText("No cabinet loaded", juce::dontSendNotification);
        mIrName.setColour(juce::Label::textColourId, theme::colours::textFaint);
        mIrDetails.setText("Drop a .wav here, or use Open. Passes through until then.",
                           juce::dontSendNotification);
    }

    mClearButton.setEnabled(name.isNotEmpty());
    repaint();
}

void IrBlockPanel::showLibraryMenu()
{
    const auto entries = mProcessor.getIrLibrary().getEntries();
    juce::PopupMenu menu;

    if (entries.isEmpty())
    {
        menu.addItem(juce::PopupMenu::Item("No IRs yet - load one and it will appear here")
                         .setEnabled(false));
    }
    else
    {
        const auto currentName = mProcessor.getIrName();
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
    menu.addItem(2, "Show IR folder");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&mLibraryButton),
                       [this, entries](int choice) {
        if (choice == 1)
            chooseIr();
        else if (choice == 2)
            mProcessor.getIrLibrary().getDirectory().revealToUser();
        else if (choice >= 100 && choice - 100 < entries.size())
            mProcessor.loadIr(entries[choice - 100].file);
    });
}

void IrBlockPanel::chooseIr()
{
    mFileChooser = std::make_unique<juce::FileChooser>("Load an impulse response", juce::File{},
                                                      IrLibrary::getWildcard());

    mFileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& chooser) {
                                  const auto file = chooser.getResult();
                                  if (file.existsAsFile())
                                      mProcessor.loadIr(file);
                              });
}

bool IrBlockPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& file : files)
        if (file.endsWithIgnoreCase(".wav") || file.endsWithIgnoreCase(".aif")
            || file.endsWithIgnoreCase(".aiff"))
            return true;

    return false;
}

void IrBlockPanel::fileDragEnter(const juce::StringArray&, int, int)
{
    mDragHighlight = true;
    repaint();
}

void IrBlockPanel::fileDragExit(const juce::StringArray&)
{
    mDragHighlight = false;
    repaint();
}

void IrBlockPanel::filesDropped(const juce::StringArray& files, int, int)
{
    mDragHighlight = false;
    repaint();

    for (const auto& file : files)
        if (isInterestedInFileDrag({file}))
        {
            mProcessor.loadIr(juce::File(file));
            break;
        }
}

void IrBlockPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto accent = getCategoryColour(BlockCategory::cabinet);

    g.setColour(theme::colours::panel);
    g.fillRect(bounds);

    auto plate = bounds.withTrimmedTop(52.0f).reduced(theme::metrics::gap, theme::metrics::gap * 0.5f);
    g.setColour(theme::colours::background);
    g.fillRoundedRectangle(plate, theme::metrics::cornerRadius);
    g.setColour(accent.withAlpha(0.20f));
    g.drawRoundedRectangle(plate, theme::metrics::cornerRadius, 1.0f);

    paintHeader(g, bounds.removeFromTop(52.0f).reduced(theme::metrics::gap, 6.0f),
                BlockCategory::cabinet);

    if (mDragHighlight)
    {
        g.setColour(accent);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), theme::metrics::cornerRadius,
                               2.0f);
    }
}

void IrBlockPanel::resized()
{
    auto area = getLocalBounds().reduced(theme::metrics::gap, 0);

    auto header = area.removeFromTop(52).withTrimmedTop(6).withTrimmedBottom(6);
    header.removeFromLeft(28);

    auto buttons = header.removeFromRight(232);
    mClearButton.setBounds(buttons.removeFromRight(54).reduced(2, 8));
    mLoadButton.setBounds(buttons.removeFromRight(74).reduced(2, 8));
    mLibraryButton.setBounds(buttons.removeFromRight(84).reduced(2, 8));

    mIrName.setBounds(header.removeFromTop(22));
    mIrDetails.setBounds(header);

    area.reduce(theme::metrics::gap, 0);
    area.removeFromTop(theme::metrics::gap);

    auto knobs = area.removeFromTop(84);
    const auto layout = [&knobs](juce::Slider& slider, juce::Label& label) {
        auto cell = knobs.removeFromLeft(88);
        label.setBounds(cell.removeFromTop(13));
        slider.setBounds(cell);
    };

    layout(mMix, mMixLabel);
    layout(mOutput, mOutputLabel);
}

//==============================================================================
UtilityBlockPanel::UtilityBlockPanel(UtilityBlockProcessor& processor)
{
    auto& state = processor.getValueTreeState();

    styleKnob(mGain);
    mGain.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff7b8494)); // utility grey
    addAndMakeVisible(mGain);
    styleCaption(mGainLabel, "Gain");
    addAndMakeVisible(mGainLabel);
    mGainAtt = std::make_unique<SliderAttachment>(state, "gain", mGain);

    styleKnob(mPan);
    mPan.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff7b8494));
    addAndMakeVisible(mPan);
    styleCaption(mPanLabel, "Pan");
    addAndMakeVisible(mPanLabel);
    mPanAtt = std::make_unique<SliderAttachment>(state, "pan", mPan);

    addAndMakeVisible(mInvertLeft);
    mInvertLeftAtt = std::make_unique<ButtonAttachment>(state, "invertL", mInvertLeft);

    addAndMakeVisible(mInvertRight);
    mInvertRightAtt = std::make_unique<ButtonAttachment>(state, "invertR", mInvertRight);

    mSwap.setTooltip("Swaps left and right. With phase invert, this is how you check what a stereo "
                     "effect is really doing.");
    addAndMakeVisible(mSwap);
    mSwapAtt = std::make_unique<ButtonAttachment>(state, "swap", mSwap);
}

void UtilityBlockPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(theme::colours::panel);
    g.fillRect(bounds);

    paintHeader(g, bounds.removeFromTop(40.0f).reduced(theme::metrics::gap, 6.0f),
                BlockCategory::utility);

    g.setColour(theme::colours::text);
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawText("Utility", getLocalBounds().withTrimmedLeft(46).removeFromTop(40),
               juce::Justification::centredLeft, false);
}

void UtilityBlockPanel::resized()
{
    auto area = getLocalBounds().reduced(theme::metrics::gap);
    area.removeFromTop(34);

    auto knobs = area.removeFromLeft(180);
    const auto layout = [&knobs](juce::Slider& slider, juce::Label& label) {
        auto cell = knobs.removeFromLeft(88);
        label.setBounds(cell.removeFromTop(13));
        slider.setBounds(cell.removeFromTop(72));
    };

    layout(mGain, mGainLabel);
    layout(mPan, mPanLabel);

    area.removeFromLeft(theme::metrics::gap);
    mInvertLeft.setBounds(area.removeFromTop(26));
    mInvertRight.setBounds(area.removeFromTop(26));
    mSwap.setBounds(area.removeFromTop(26));
}

//==============================================================================
EqBlockPanel::EqBlockPanel(juce::AudioProcessor&, juce::AudioProcessorValueTreeState& state)
{
    // Column per band, row per control. The grid IS the mental model of an EQ,
    // so the layout mirrors it exactly.
    addControl(state, "hp_on", "On", 0, 0, true);
    addControl(state, "hp_freq", "High-pass", 0, 1, false);

    addControl(state, "ls_freq", "Low shelf", 1, 1, false);
    addControl(state, "ls_gain", "Gain", 1, 2, false);

    addControl(state, "b1_freq", "Bell 1", 2, 1, false);
    addControl(state, "b1_gain", "Gain", 2, 2, false);
    addControl(state, "b1_q", "Q", 2, 3, false);

    addControl(state, "b2_freq", "Bell 2", 3, 1, false);
    addControl(state, "b2_gain", "Gain", 3, 2, false);
    addControl(state, "b2_q", "Q", 3, 3, false);

    addControl(state, "hs_freq", "High shelf", 4, 1, false);
    addControl(state, "hs_gain", "Gain", 4, 2, false);

    addControl(state, "lp_on", "On", 5, 0, true);
    addControl(state, "lp_freq", "Low-pass", 5, 1, false);
}

void EqBlockPanel::addControl(juce::AudioProcessorValueTreeState& state, const juce::String& id,
                              const juce::String& caption, int column, int row, bool isToggle)
{
    auto control = std::make_unique<Control>();
    control->column = column;
    control->row = row;

    if (isToggle)
    {
        auto button = std::make_unique<juce::ToggleButton>();
        control->buttonAtt =
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, id, *button);
        addAndMakeVisible(*button);
        control->widget = std::move(button);
    }
    else
    {
        auto slider = std::make_unique<juce::Slider>();
        styleKnob(*slider);
        slider->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff4c8dff)); // EQ blue
        control->sliderAtt =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, id, *slider);
        addAndMakeVisible(*slider);
        control->widget = std::move(slider);
    }

    styleCaption(control->caption, caption);
    addAndMakeVisible(control->caption);

    mControls.push_back(std::move(control));
}

void EqBlockPanel::paint(juce::Graphics& g)
{
    g.fillAll(theme::colours::panel);

    // Hairlines between bands, so the columns read as bands rather than a wall
    // of knobs.
    const auto columnWidth = static_cast<float>(getWidth()) / 6.0f;
    g.setColour(theme::colours::outline);

    for (int i = 1; i < 6; ++i)
        g.fillRect(columnWidth * i, 28.0f, 1.0f, static_cast<float>(getHeight()) - 40.0f);
}

void EqBlockPanel::resized()
{
    const auto columnWidth = getWidth() / 6;
    constexpr int rowHeight = 60;

    for (auto& control : mControls)
    {
        const auto x = control->column * columnWidth;
        const auto y = 24 + control->row * rowHeight;

        auto cell = juce::Rectangle<int>(x, y, columnWidth, rowHeight).reduced(4, 2);
        control->caption.setBounds(cell.removeFromTop(13));

        if (dynamic_cast<juce::ToggleButton*>(control->widget.get()) != nullptr)
            control->widget->setBounds(cell.withSizeKeepingCentre(30, 24));
        else
            control->widget->setBounds(cell);
    }
}

} // namespace blockrig
