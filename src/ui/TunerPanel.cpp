#include "ui/TunerPanel.h"

#include "ui/Theme.h"

namespace blockrig
{
namespace
{
const char* kNoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/// How long a note keeps displaying after the signal fades, so the readout does
/// not vanish between pluck and glance.
constexpr int kHoldMs = 1200;
} // namespace

TunerPanel::TunerPanel(BlockRigProcessor& processor)
    : mProcessor(processor)
{
    const auto styleModeButton = [this](juce::TextButton& button, Mode mode) {
        button.setClickingTogglesState(false);
        button.onClick = [this, mode] {
            mMode = mode;
            mNeedleButton.setToggleState(mMode == Mode::needle, juce::dontSendNotification);
            mStrobeButton.setToggleState(mMode == Mode::strobe, juce::dontSendNotification);
            repaint();
        };
        addAndMakeVisible(button);
    };

    styleModeButton(mNeedleButton, Mode::needle);
    styleModeButton(mStrobeButton, Mode::strobe);
    mNeedleButton.setToggleState(true, juce::dontSendNotification);

    // 440 is the default; the others cover orchestral and baroque habits.
    mReference.addItemList({"435 Hz", "438 Hz", "440 Hz", "442 Hz", "444 Hz"}, 1);
    mReference.setSelectedId(3, juce::dontSendNotification);
    mReference.setTooltip("Reference pitch for A4");
    mReference.onChange = [this] {
        mReferenceHz = mReference.getText().getDoubleValue();
        if (mReferenceHz < 100.0)
            mReferenceHz = 440.0;
    };
    addAndMakeVisible(mReference);

    mProcessor.setTunerActive(true);
    startTimerHz(30);
}

TunerPanel::~TunerPanel()
{
    stopTimer();
    mProcessor.setTunerActive(false);
}

void TunerPanel::timerCallback()
{
    const auto result = mProcessor.getPitchDetector().analyse();
    mLevel = result.level;

    if (result.frequency > 20.0f && result.clarity > 0.5f)
    {
        mDisplayedFrequency = result.frequency;
        mClarity = result.clarity;
        mLastSignalMs = juce::Time::currentTimeMillis();

        // Note number and cents against the chosen reference.
        const auto midi = 69.0 + 12.0 * std::log2(result.frequency / mReferenceHz);
        mNoteNumber = static_cast<int>(std::round(midi));
        const auto cents = static_cast<float>((midi - mNoteNumber) * 100.0);

        // Smooth the needle: raw YIN jitters by a couple of cents on a real
        // guitar signal, and a needle that vibrates is unreadable.
        mSmoothedCents = mSmoothedCents * 0.7f + cents * 0.3f;

        // Strobe drifts at a rate proportional to detune; stands still in tune.
        mStrobePhase += mSmoothedCents * 0.004;
    }
    else if (juce::Time::currentTimeMillis() - mLastSignalMs > kHoldMs)
    {
        mNoteNumber = -1;
        mDisplayedFrequency = 0.0f;
        mSmoothedCents = 0.0f;
    }

    repaint();
}

void TunerPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(theme::colours::background);
    g.fillRect(bounds);

    auto area = bounds.reduced(18.0f);
    area.removeFromTop(40.0f); // controls row, laid out in resized()

    const bool inTune = mNoteNumber >= 0 && std::abs(mSmoothedCents) < 3.0f;
    const auto accent = inTune ? theme::colours::good
                               : (mNoteNumber >= 0 ? theme::colours::accent : theme::colours::textFaint);

    // The note, big enough to read from across a room.
    auto noteArea = area.removeFromTop(area.getHeight() * 0.42f);

    if (mNoteNumber >= 0)
    {
        const auto name = juce::String(kNoteNames[((mNoteNumber % 12) + 12) % 12]);
        const auto octave = juce::String(mNoteNumber / 12 - 1);

        g.setColour(accent);
        g.setFont(juce::FontOptions(64.0f, juce::Font::bold));
        g.drawText(name, noteArea, juce::Justification::centred, false);

        g.setColour(theme::colours::textDim);
        g.setFont(juce::FontOptions(20.0f));
        g.drawText(octave, noteArea.translated(46.0f, 14.0f), juce::Justification::centred, false);

        g.setFont(juce::FontOptions(12.0f));
        g.setColour(theme::colours::textFaint);
        g.drawText(juce::String(mDisplayedFrequency, 1) + " Hz   "
                       + (mSmoothedCents >= 0 ? "+" : "") + juce::String(mSmoothedCents, 1) + " cents",
                   noteArea.removeFromBottom(18.0f), juce::Justification::centred, false);
    }
    else
    {
        g.setColour(theme::colours::textFaint);
        g.setFont(juce::FontOptions(17.0f));
        g.drawText(mLevel > 1.0e-4f ? "Listening…" : "Play a note",
                   noteArea, juce::Justification::centred, false);
    }

    area.removeFromTop(8.0f);

    if (mMode == Mode::needle)
        drawNeedle(g, area);
    else
        drawStrobe(g, area);
}

void TunerPanel::drawNeedle(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto centreX = area.getCentreX();
    const bool active = mNoteNumber >= 0;
    const bool inTune = active && std::abs(mSmoothedCents) < 3.0f;

    // Scale: ±50 cents across the width, ticks every 10.
    const auto scale = area.reduced(area.getWidth() * 0.06f, 0.0f);

    for (int cents = -50; cents <= 50; cents += 10)
    {
        const auto x = scale.getX() + scale.getWidth() * (cents + 50) / 100.0f;
        const bool major = cents == 0;

        g.setColour(major ? theme::colours::text : theme::colours::outlineStrong);
        g.fillRect(x - (major ? 1.0f : 0.5f), area.getY(), major ? 2.0f : 1.0f,
                   area.getHeight() * (major ? 0.55f : 0.35f));

        if (cents % 20 == 0)
        {
            g.setColour(theme::colours::textFaint);
            g.setFont(juce::FontOptions(9.5f));
            g.drawText(juce::String(cents), juce::Rectangle<float>(x - 16.0f, area.getBottom() - 16.0f,
                                                                   32.0f, 12.0f),
                       juce::Justification::centred, false);
        }
    }

    // In-tune window, ±3 cents.
    const auto windowLeft = scale.getX() + scale.getWidth() * 47.0f / 100.0f;
    const auto windowRight = scale.getX() + scale.getWidth() * 53.0f / 100.0f;
    g.setColour(theme::colours::good.withAlpha(0.14f));
    g.fillRect(windowLeft, area.getY(), windowRight - windowLeft, area.getHeight() * 0.55f);

    if (active)
    {
        const auto clamped = juce::jlimit(-50.0f, 50.0f, mSmoothedCents);
        const auto x = scale.getX() + scale.getWidth() * (clamped + 50.0f) / 100.0f;

        g.setColour(inTune ? theme::colours::good : theme::colours::accent);
        g.fillRoundedRectangle(x - 2.0f, area.getY() - 4.0f, 4.0f, area.getHeight() * 0.62f, 2.0f);
    }
    else
    {
        g.setColour(theme::colours::outlineStrong);
        g.fillRoundedRectangle(centreX - 2.0f, area.getY() - 4.0f, 4.0f, area.getHeight() * 0.62f, 2.0f);
    }
}

void TunerPanel::drawStrobe(juce::Graphics& g, juce::Rectangle<float> area)
{
    const bool active = mNoteNumber >= 0;
    const bool inTune = active && std::abs(mSmoothedCents) < 3.0f;
    const auto band = area.withHeight(area.getHeight() * 0.6f);

    // Bars scroll left when flat, right when sharp, freeze when in tune.
    constexpr int kBars = 14;
    const auto barWidth = band.getWidth() / kBars;
    const auto offset = static_cast<float>(std::fmod(mStrobePhase, 1.0)) * barWidth * 2.0f;

    g.saveState();
    g.reduceClipRegion(band.toNearestInt());

    for (int i = -2; i < kBars + 2; i += 2)
    {
        const auto x = band.getX() + i * barWidth + offset;
        g.setColour((inTune ? theme::colours::good : theme::colours::accent)
                        .withAlpha(active ? (inTune ? 0.8f : 0.45f) : 0.12f));
        g.fillRect(x, band.getY(), barWidth, band.getHeight());
    }

    g.restoreState();

    g.setColour(theme::colours::outlineStrong);
    g.drawRect(band, 1.0f);

    g.setColour(theme::colours::textFaint);
    g.setFont(juce::FontOptions(10.5f));
    g.drawText(active ? (inTune ? "in tune" : (mSmoothedCents < 0 ? "flat — drifting left"
                                                                  : "sharp — drifting right"))
                      : "waiting for a note",
               area.removeFromBottom(16.0f), juce::Justification::centred, false);
}

void TunerPanel::resized()
{
    auto controls = getLocalBounds().reduced(18, 0).removeFromTop(48).withTrimmedTop(14);

    mNeedleButton.setBounds(controls.removeFromLeft(76).reduced(0, 4));
    controls.removeFromLeft(6);
    mStrobeButton.setBounds(controls.removeFromLeft(76).reduced(0, 4));
    mReference.setBounds(controls.removeFromRight(92).reduced(0, 4));
}

} // namespace blockrig
