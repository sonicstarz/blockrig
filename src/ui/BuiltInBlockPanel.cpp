#include "ui/BuiltInBlockPanel.h"

#include <map>

#include <juce_audio_formats/juce_audio_formats.h>

#include "ui/BlockCategories.h"

namespace blockrig
{
namespace
{
/// Freq values read as musicians write them: "500 Hz", "2.50 kHz".
juce::String frequencyText(double value)
{
    return value < 1000.0 ? juce::String(juce::roundToInt(value)) + " Hz"
                          : juce::String(value / 1000.0, 2) + " kHz";
}

juce::String gainText(double value)
{
    return (value >= 0.0 ? "+" : "") + juce::String(value, 1) + " dB";
}
} // namespace

//==============================================================================
IrBlockPanel::IrBlockPanel(IrBlockProcessor& processor)
    : mProcessor(processor)
{
    const auto teal = getCategoryColour(BlockCategory::cabinet);

    mLibraryButton.setTooltip("Every IR you load is collected here automatically.");
    mLibraryButton.onClick = [this] { showLibraryMenu(); };

    mLoadButton.setTooltip("Load a .wav or .aiff impulse response.");
    mLoadButton.onClick = [this] { chooseIr(); };

    mClearButton.onClick = [this] { mProcessor.clearIr(); };

    mTitleBarRow.add(mLibraryButton, 70);
    mTitleBarRow.add(mLoadButton, 68);
    mTitleBarRow.add(mClearButton, 56);

    theme::editor::styleKnob(mMix, teal);
    addAndMakeVisible(mMix);
    theme::editor::styleCaption(mMixLabel, "Mix");
    addAndMakeVisible(mMixLabel);
    mMixAtt = std::make_unique<SliderAttachment>(mProcessor.getValueTreeState(), "mix", mMix);

    theme::editor::styleKnob(mOutput, teal);
    addAndMakeVisible(mOutput);
    theme::editor::styleCaption(mOutputLabel, "Output");
    addAndMakeVisible(mOutputLabel);
    mOutputAtt = std::make_unique<SliderAttachment>(mProcessor.getValueTreeState(), "out_trim", mOutput);

    mProcessor.onIrChanged = [this] { refresh(); };
    refresh();
}

IrBlockPanel::~IrBlockPanel()
{
    mProcessor.onIrChanged = nullptr;
}

void IrBlockPanel::setSubtitle(const juce::String& subtitle)
{
    mSubtitle = subtitle;
    if (onSubtitleChanged)
        onSubtitleChanged(mSubtitle);
}

void IrBlockPanel::refresh()
{
    const auto name = mProcessor.getIrName();
    setSubtitle(name.isNotEmpty() ? mProcessor.getIrFile().getFileName()
                                  : juce::String("no cabinet loaded"));
    mClearButton.setEnabled(name.isNotEmpty());
    loadWaveform();
    repaint();
}

void IrBlockPanel::loadWaveform()
{
    mWaveform.clear();
    mWaveformCaption.clear();

    const auto file = mProcessor.getIrFile();
    if (!file.existsAsFile())
        return;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return;

    // IRs are short; cap the read so a mislabelled long file cannot stall the
    // message thread.
    const auto numSamples =
        static_cast<int>(juce::jmin<juce::int64>(reader->lengthInSamples,
                                                 static_cast<juce::int64>(reader->sampleRate * 4)));

    juce::AudioBuffer<float> buffer(1, numSamples);
    reader->read(&buffer, 0, numSamples, 0, true, false);

    // One signed peak per bucket: the sample with the largest magnitude keeps
    // its sign, so the polyline oscillates the way the mock's does.
    constexpr int kPoints = 240;
    const auto* data = buffer.getReadPointer(0);
    const auto bucket = juce::jmax(1, numSamples / kPoints);

    float maxAbs = 0.0f;
    mWaveform.reserve(static_cast<size_t>(kPoints));

    for (int start = 0; start < numSamples; start += bucket)
    {
        float peak = 0.0f;
        for (int i = start; i < juce::jmin(start + bucket, numSamples); ++i)
            if (std::abs(data[i]) > std::abs(peak))
                peak = data[i];

        mWaveform.push_back(peak);
        maxAbs = juce::jmax(maxAbs, std::abs(peak));
    }

    if (maxAbs > 0.0f)
        for (auto& value : mWaveform)
            value /= maxAbs;

    const auto lengthMs = juce::roundToInt(1000.0 * reader->lengthInSamples / reader->sampleRate);
    mWaveformCaption = juce::String(lengthMs) + juce::String::fromUTF8(" ms \xc2\xb7 ")
                       + juce::String(juce::roundToInt(reader->sampleRate / 1000.0)) + " kHz";
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
    const auto teal = getCategoryColour(BlockCategory::cabinet);

    theme::editor::paintWell(g, mWell);

    if (!mWaveform.empty())
    {
        const auto plot = mWell.reduced(12.0f, 14.0f);
        juce::Path line;

        for (size_t i = 0; i < mWaveform.size(); ++i)
        {
            const auto x = plot.getX()
                           + plot.getWidth() * static_cast<float>(i)
                                 / static_cast<float>(mWaveform.size() - 1);
            const auto y = plot.getCentreY() - mWaveform[i] * plot.getHeight() * 0.48f;

            if (i == 0)
                line.startNewSubPath(x, y);
            else
                line.lineTo(x, y);
        }

        g.setColour(teal);
        g.strokePath(line, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }
    else
    {
        g.setColour(theme::colours::textGhost);
        g.setFont(theme::fonts::ui(12.0f));
        g.drawText("Drop a .wav here, or use Open...", mWell, juce::Justification::centred, true);
    }

    if (mWaveformCaption.isNotEmpty())
    {
        g.setColour(theme::colours::textGhost);
        g.setFont(theme::fonts::mono(10.0f));
        g.drawText(mWaveformCaption, mWell.reduced(12.0f, 8.0f), juce::Justification::bottomRight,
                   true);
    }

    if (mDragHighlight)
    {
        g.setColour(getCategoryColour(BlockCategory::cabinet));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), theme::metrics::radiusMd,
                               2.0f);
    }
}

void IrBlockPanel::resized()
{
    auto area = getLocalBounds().reduced(theme::metrics::padding, theme::metrics::gap);

    auto knobs = area.removeFromLeft(2 * theme::editor::cellWidth);
    knobs = knobs.withSizeKeepingCentre(knobs.getWidth(), theme::editor::cellHeight);
    theme::editor::layoutKnobCell(knobs.removeFromLeft(theme::editor::cellWidth), mMix, mMixLabel);
    theme::editor::layoutKnobCell(knobs, mOutput, mOutputLabel);

    area.removeFromLeft(theme::metrics::gap);
    mWell = area.toFloat();
}

//==============================================================================
UtilityBlockPanel::UtilityBlockPanel(UtilityBlockProcessor& processor)
{
    auto& state = processor.getValueTreeState();
    const auto grey = getCategoryColour(BlockCategory::utility);

    theme::editor::styleKnob(mGain, grey);
    addAndMakeVisible(mGain);
    theme::editor::styleCaption(mGainLabel, "Gain");
    addAndMakeVisible(mGainLabel);
    mGainAtt = std::make_unique<SliderAttachment>(state, "gain", mGain);

    theme::editor::styleKnob(mPan, grey);
    addAndMakeVisible(mPan);
    theme::editor::styleCaption(mPanLabel, "Pan");
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

void UtilityBlockPanel::resized()
{
    auto area = getLocalBounds().reduced(theme::metrics::padding, theme::metrics::gap);

    auto knobs = area.removeFromLeft(2 * theme::editor::cellWidth);
    knobs = knobs.withSizeKeepingCentre(knobs.getWidth(), theme::editor::cellHeight);
    theme::editor::layoutKnobCell(knobs.removeFromLeft(theme::editor::cellWidth), mGain, mGainLabel);
    theme::editor::layoutKnobCell(knobs, mPan, mPanLabel);

    area.removeFromLeft(theme::metrics::padding);
    auto switches = area.withSizeKeepingCentre(area.getWidth(), 3 * 26);
    mInvertLeft.setBounds(switches.removeFromTop(26));
    mInvertRight.setBounds(switches.removeFromTop(26));
    mSwap.setBounds(switches.removeFromTop(26));
}

//==============================================================================
void EqBlockPanel::BandChipRow::setSelected(int band)
{
    mSelected = band;
    repaint();
}

int EqBlockPanel::BandChipRow::getPreferredWidth() const
{
    return static_cast<int>(kBands.size()) * kChipWidth
           + (static_cast<int>(kBands.size()) - 1) * kChipGap;
}

void EqBlockPanel::BandChipRow::paint(juce::Graphics& g)
{
    const auto blue = getCategoryColour(BlockCategory::eq);
    auto area = getLocalBounds();

    for (size_t i = 0; i < kBands.size(); ++i)
    {
        const auto chip = area.removeFromLeft(kChipWidth).withSizeKeepingCentre(kChipWidth, 24).toFloat();
        area.removeFromLeft(kChipGap);

        if (static_cast<int>(i) == mSelected)
        {
            g.setColour(blue);
            g.fillRoundedRectangle(chip, theme::metrics::radiusSm);
            g.setColour(theme::colours::background);
        }
        else
        {
            g.setColour(theme::colours::outline);
            g.drawRoundedRectangle(chip.reduced(0.5f), theme::metrics::radiusSm, 1.0f);
            g.setColour(theme::colours::textFaint);
        }

        g.setFont(theme::fonts::mono(11.0f, 600));
        g.drawText(kBands[i].chipLabel, chip, juce::Justification::centred, false);
    }
}

void EqBlockPanel::BandChipRow::mouseDown(const juce::MouseEvent& event)
{
    const auto index = event.x / (kChipWidth + kChipGap);

    if (index >= 0 && index < static_cast<int>(kBands.size()) && onSelect)
        onSelect(index);
}

//==============================================================================
EqBlockPanel::Graph::Graph(EqBlockProcessor& processor)
    : mProcessor(processor)
{
    startTimerHz(15);
}

EqBlockPanel::Graph::~Graph()
{
    stopTimer();
}

void EqBlockPanel::Graph::setSelectedBand(int band)
{
    mSelected = band;
    repaint();
}

float EqBlockPanel::Graph::frequencyToX(float frequency) const
{
    const auto plot = getLocalBounds().toFloat().reduced(1.0f, 0.0f);
    return plot.getX()
           + plot.getWidth() * std::log10(juce::jmax(frequency, 20.0f) / 20.0f) / 3.0f;
}

float EqBlockPanel::Graph::xToFrequency(float x) const
{
    const auto plot = getLocalBounds().toFloat().reduced(1.0f, 0.0f);
    const auto proportion = juce::jlimit(0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
    return 20.0f * std::pow(10.0f, proportion * 3.0f);
}

float EqBlockPanel::Graph::decibelsToY(float decibels) const
{
    const auto bounds = getLocalBounds().toFloat();
    const auto scale = (bounds.getHeight() * 0.5f - 10.0f) / 18.0f;
    return bounds.getCentreY() - juce::jlimit(-18.0f, 18.0f, decibels) * scale;
}

float EqBlockPanel::Graph::responseDb(float frequency) const
{
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    auto& state = mProcessor.getValueTreeState();
    const auto value = [&state](const juce::String& id) {
        return state.getRawParameterValue(id)->load();
    };
    const auto on = [&value](const juce::String& id) { return value(id) >= 0.5f; };

    auto sampleRate = mProcessor.getSampleRate();
    if (sampleRate <= 0.0)
        sampleRate = 48000.0;

    const auto nyquist = static_cast<float>(sampleRate * 0.5);
    const auto clamp = [nyquist](float f) { return juce::jlimit(20.0f, nyquist * 0.95f, f); };

    // The same construction the processor uses, so the drawn curve is the
    // filter, not an approximation of it.
    double magnitude = 1.0;

    if (on("hp_on"))
        magnitude *= Coefficients::makeHighPass(sampleRate, clamp(value("hp_freq")))
                         ->getMagnitudeForFrequency(frequency, sampleRate);
    if (on("ls_on"))
        magnitude *= Coefficients::makeLowShelf(sampleRate, clamp(value("ls_freq")), 0.7f,
                                                juce::Decibels::decibelsToGain(value("ls_gain")))
                         ->getMagnitudeForFrequency(frequency, sampleRate);
    if (on("b1_on"))
        magnitude *= Coefficients::makePeakFilter(sampleRate, clamp(value("b1_freq")),
                                                  juce::jmax(0.1f, value("b1_q")),
                                                  juce::Decibels::decibelsToGain(value("b1_gain")))
                         ->getMagnitudeForFrequency(frequency, sampleRate);
    if (on("b2_on"))
        magnitude *= Coefficients::makePeakFilter(sampleRate, clamp(value("b2_freq")),
                                                  juce::jmax(0.1f, value("b2_q")),
                                                  juce::Decibels::decibelsToGain(value("b2_gain")))
                         ->getMagnitudeForFrequency(frequency, sampleRate);
    if (on("hs_on"))
        magnitude *= Coefficients::makeHighShelf(sampleRate, clamp(value("hs_freq")), 0.7f,
                                                 juce::Decibels::decibelsToGain(value("hs_gain")))
                         ->getMagnitudeForFrequency(frequency, sampleRate);
    if (on("lp_on"))
        magnitude *= Coefficients::makeLowPass(sampleRate, clamp(value("lp_freq")))
                         ->getMagnitudeForFrequency(frequency, sampleRate);

    return juce::Decibels::gainToDecibels(static_cast<float>(magnitude), -60.0f);
}

juce::Point<float> EqBlockPanel::Graph::handleCentre(int band) const
{
    auto& state = mProcessor.getValueTreeState();
    const auto frequency =
        state.getRawParameterValue(juce::String(kBands[static_cast<size_t>(band)].prefix) + "_freq")
            ->load();

    return {frequencyToX(frequency), decibelsToY(responseDb(frequency))};
}

void EqBlockPanel::Graph::paint(juce::Graphics& g)
{
    const auto blue = getCategoryColour(BlockCategory::eq);
    const auto bounds = getLocalBounds().toFloat();

    theme::editor::paintWell(g, bounds);

    // Grid: frequency decades and the 0 dB line.
    g.setColour(theme::colours::hairline);
    for (const float f : {100.0f, 1000.0f, 10000.0f})
        g.fillRect(frequencyToX(f), bounds.getY() + 1.0f, 1.0f, bounds.getHeight() - 2.0f);
    g.fillRect(bounds.getX() + 1.0f, bounds.getCentreY(), bounds.getWidth() - 2.0f, 1.0f);

    // The curve, sampled every couple of pixels. (Path::isEmpty() ignores
    // moveTos, so track the start explicitly.)
    juce::Path curve;
    const auto step = 2.0f;
    bool started = false;

    for (auto x = bounds.getX() + 1.0f; x <= bounds.getRight() - 1.0f; x += step)
    {
        const auto y = decibelsToY(responseDb(xToFrequency(x)));
        if (!started)
        {
            curve.startNewSubPath(x, y);
            started = true;
        }
        else
        {
            curve.lineTo(x, y);
        }
    }

    // Area fill beneath the curve, then the stroke on top.
    auto area = curve;
    area.lineTo(bounds.getRight() - 1.0f, bounds.getBottom() - 1.0f);
    area.lineTo(bounds.getX() + 1.0f, bounds.getBottom() - 1.0f);
    area.closeSubPath();
    g.setColour(blue.withAlpha(0.08f));
    g.fillPath(area);

    g.setColour(blue);
    g.strokePath(curve, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // Handles: selected = filled with a light ring; others = outlined.
    auto& state = mProcessor.getValueTreeState();

    for (int band = 0; band < static_cast<int>(kBands.size()); ++band)
    {
        const auto centre = handleCentre(band);
        const auto enabled =
            state.getRawParameterValue(juce::String(kBands[static_cast<size_t>(band)].prefix) + "_on")
                ->load() >= 0.5f;
        const auto colour = enabled ? blue : blue.withAlpha(0.45f);

        if (band == mSelected)
        {
            const auto radius = 7.0f;
            g.setColour(colour);
            g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
            g.setColour(theme::colours::text.withAlpha(0.85f));
            g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 2.0f);
        }
        else
        {
            const auto radius = 5.5f;
            g.setColour(theme::colours::inset);
            g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
            g.setColour(colour);
            g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 2.0f);
        }
    }
}

void EqBlockPanel::Graph::beginDrag(int band)
{
    auto& state = mProcessor.getValueTreeState();
    const auto& info = kBands[static_cast<size_t>(band)];

    if (auto* freq = state.getParameter(juce::String(info.prefix) + "_freq"))
        freq->beginChangeGesture();
    if (info.hasGain)
        if (auto* gain = state.getParameter(juce::String(info.prefix) + "_gain"))
            gain->beginChangeGesture();

    mDragging = band;
}

void EqBlockPanel::Graph::endDrag()
{
    if (mDragging < 0)
        return;

    auto& state = mProcessor.getValueTreeState();
    const auto& info = kBands[static_cast<size_t>(mDragging)];

    if (auto* freq = state.getParameter(juce::String(info.prefix) + "_freq"))
        freq->endChangeGesture();
    if (info.hasGain)
        if (auto* gain = state.getParameter(juce::String(info.prefix) + "_gain"))
            gain->endChangeGesture();

    mDragging = -1;
}

void EqBlockPanel::Graph::mouseDown(const juce::MouseEvent& event)
{
    // Nearest handle within reach wins; prefer the already-selected band on a
    // tie so stacked handles stay predictable.
    auto best = -1;
    auto bestDistance = 20.0f;

    for (int band = 0; band < static_cast<int>(kBands.size()); ++band)
    {
        const auto distance = handleCentre(band).getDistanceFrom(event.position)
                              - (band == mSelected ? 0.5f : 0.0f);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = band;
        }
    }

    if (best >= 0)
    {
        if (best != mSelected && onBandSelected)
            onBandSelected(best); // panel calls setSelectedBand back
        beginDrag(best);
    }
}

void EqBlockPanel::Graph::mouseDrag(const juce::MouseEvent& event)
{
    if (mDragging < 0)
        return;

    auto& state = mProcessor.getValueTreeState();
    const auto& info = kBands[static_cast<size_t>(mDragging)];

    if (auto* freq = state.getParameter(juce::String(info.prefix) + "_freq"))
    {
        const auto& range = freq->getNormalisableRange();
        const auto target = juce::jlimit(range.start, range.end, xToFrequency(event.position.x));
        freq->setValueNotifyingHost(range.convertTo0to1(target));
    }

    if (info.hasGain)
        if (auto* gain = state.getParameter(juce::String(info.prefix) + "_gain"))
        {
            const auto bounds = getLocalBounds().toFloat();
            const auto scale = (bounds.getHeight() * 0.5f - 10.0f) / 18.0f;
            const auto target =
                juce::jlimit(-18.0f, 18.0f, (bounds.getCentreY() - event.position.y) / scale);
            gain->setValueNotifyingHost(gain->getNormalisableRange().convertTo0to1(target));
        }
}

void EqBlockPanel::Graph::mouseUp(const juce::MouseEvent&)
{
    endDrag();
}

void EqBlockPanel::Graph::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto& info = kBands[static_cast<size_t>(mSelected)];
    if (!info.hasQ)
        return;

    auto& state = mProcessor.getValueTreeState();
    if (auto* q = state.getParameter(juce::String(info.prefix) + "_q"))
        q->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, q->getValue() + wheel.deltaY * 0.5f));
}

void EqBlockPanel::Graph::timerCallback()
{
    // Repaint when any parameter the curve depends on has moved — covers MIDI
    // and automation as well as our own drags, per the MainView timer pattern.
    auto& state = mProcessor.getValueTreeState();
    std::array<float, 20> snapshot{};
    size_t index = 0;

    for (const auto& band : kBands)
    {
        const juce::String prefix(band.prefix);
        snapshot[index++] = state.getRawParameterValue(prefix + "_on")->load();
        snapshot[index++] = state.getRawParameterValue(prefix + "_freq")->load();
        if (band.hasGain)
            snapshot[index++] = state.getRawParameterValue(prefix + "_gain")->load();
        if (band.hasQ)
            snapshot[index++] = state.getRawParameterValue(prefix + "_q")->load();
    }

    if (snapshot != mLastSnapshot)
    {
        mLastSnapshot = snapshot;
        repaint();
    }
}

//==============================================================================
EqBlockPanel::EqBlockPanel(EqBlockProcessor& processor)
    : mProcessor(processor)
    , mGraph(processor)
{
    const auto blue = getCategoryColour(BlockCategory::eq);

    addAndMakeVisible(mGraph);
    mGraph.onBandSelected = [this](int band) { selectBand(band); };
    mChips.onSelect = [this](int band) { selectBand(band); };

    theme::editor::styleKnob(mFreq, blue);
    addAndMakeVisible(mFreq);
    theme::editor::styleCaption(mFreqLabel, "Freq");
    addAndMakeVisible(mFreqLabel);

    theme::editor::styleKnob(mGainKnob, blue);
    addAndMakeVisible(mGainKnob);
    theme::editor::styleCaption(mGainLabel, "Gain");
    addAndMakeVisible(mGainLabel);

    theme::editor::styleKnob(mQ, blue);
    addAndMakeVisible(mQ);
    theme::editor::styleCaption(mQLabel, "Q");
    addAndMakeVisible(mQLabel);

    addAndMakeVisible(mBandOn);

    mHint.setText("Double-tap value to type exact", juce::dontSendNotification);
    mHint.setFont(theme::fonts::ui(11.0f));
    mHint.setColour(juce::Label::textColourId, theme::colours::textGhost);
    addAndMakeVisible(mHint);

    selectBand(2); // B1, matching the mock's initial state
}

juce::Component* EqBlockPanel::getTitleBarRow()
{
    return &mChips;
}

int EqBlockPanel::getTitleBarRowWidth() const
{
    return mChips.getPreferredWidth();
}

void EqBlockPanel::selectBand(int band)
{
    const auto& info = kBands[static_cast<size_t>(band)];
    auto& state = mProcessor.getValueTreeState();
    const juce::String prefix(info.prefix);

    mChips.setSelected(band);
    mGraph.setSelectedBand(band);

    mFreqAtt = std::make_unique<SliderAttachment>(state, prefix + "_freq", mFreq);
    mFreq.textFromValueFunction = [](double v) { return frequencyText(v); };
    mFreq.updateText();

    if (info.hasGain)
    {
        mGainAtt = std::make_unique<SliderAttachment>(state, prefix + "_gain", mGainKnob);
        mGainKnob.textFromValueFunction = [](double v) { return gainText(v); };
        mGainKnob.setEnabled(true);
    }
    else
    {
        mGainAtt.reset();
        mGainKnob.textFromValueFunction = [](double) { return juce::String("-"); };
        mGainKnob.setEnabled(false);
    }
    mGainKnob.updateText();

    if (info.hasQ)
    {
        mQAtt = std::make_unique<SliderAttachment>(state, prefix + "_q", mQ);
        mQ.textFromValueFunction = [](double v) { return juce::String(v, 2); };
        mQ.setEnabled(true);
    }
    else
    {
        mQAtt.reset();
        mQ.textFromValueFunction = [](double) { return juce::String("-"); };
        mQ.setEnabled(false);
    }
    mQ.updateText();

    mBandOnAtt = std::make_unique<ButtonAttachment>(state, prefix + "_on", mBandOn);
}

void EqBlockPanel::resized()
{
    auto area = getLocalBounds().reduced(theme::metrics::padding, theme::metrics::gap);

    auto row = area.removeFromBottom(theme::editor::cellHeight);
    area.removeFromBottom(theme::metrics::gap);
    mGraph.setBounds(area);

    theme::editor::layoutKnobCell(row.removeFromLeft(theme::editor::cellWidth), mFreq, mFreqLabel);
    theme::editor::layoutKnobCell(row.removeFromLeft(theme::editor::cellWidth), mGainKnob, mGainLabel);
    theme::editor::layoutKnobCell(row.removeFromLeft(theme::editor::cellWidth), mQ, mQLabel);

    row.removeFromLeft(theme::metrics::padding);
    auto side = row.withSizeKeepingCentre(row.getWidth(), 48);
    mBandOn.setBounds(side.removeFromTop(24));
    mHint.setBounds(side.removeFromTop(20));
}

} // namespace blockrig
