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

constexpr float kScaleWidth = 480.0f;
constexpr float kPadWidth = 72.0f;
constexpr float kPadHeight = 44.0f;
} // namespace

//==============================================================================
void TunerPanel::Segmented::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto radius = theme::metrics::radiusMd;

    g.setColour(theme::colours::panel);
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(theme::colours::outline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);

    const auto half = bounds.getWidth() * 0.5f;
    const char* labels[] = {"Needle", "Strobe"};

    for (int i = 0; i < 2; ++i)
    {
        const auto segment = bounds.withX(bounds.getX() + i * half).withWidth(half);

        if (i == mSelected)
        {
            g.setColour(theme::colours::accent);
            g.fillRoundedRectangle(segment.reduced(3.0f), radius - 2.0f);
        }

        g.setColour(i == mSelected ? theme::colours::onAccent : theme::colours::textFaint);
        g.setFont(theme::fonts::ui(15.0f, i == mSelected ? 700 : 500));
        g.drawText(labels[i], segment, juce::Justification::centred, false);
    }
}

void TunerPanel::Segmented::mouseUp(const juce::MouseEvent& event)
{
    if (!contains(event.getPosition()))
        return;

    const auto index = event.position.x < getWidth() * 0.5f ? 0 : 1;
    setSelected(index);

    if (onSelect)
        onSelect(index);
}

//==============================================================================
TunerPanel::TunerPanel(BlockRigProcessor& processor)
    : mProcessor(processor)
{
    mSegmented.onSelect = [this](int index) {
        mMode = index == 0 ? Mode::needle : Mode::strobe;
        repaint();
    };
    addAndMakeVisible(mSegmented);

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

int TunerPanel::detectedString() const
{
    if (mNoteNumber < 0)
        return -1;

    int best = -1, bestDistance = 4; // within a major third counts as that string

    for (int i = 0; i < static_cast<int>(kStringNotes.size()); ++i)
    {
        const auto distance = std::abs(mNoteNumber - kStringNotes[static_cast<size_t>(i)]);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return best;
}

void TunerPanel::mouseUp(const juce::MouseEvent& event)
{
    if (mCloseButton.contains(event.position) && onClose)
        onClose();
}

void TunerPanel::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Radial-tinted black, per the boot/tuner backdrop token.
    juce::ColourGradient backdrop(juce::Colour(0xff12101a), bounds.getCentreX(), bounds.getCentreY(),
                                  theme::colours::background, bounds.getCentreX(), bounds.getBottom(),
                                  true);
    g.setGradientFill(backdrop);
    g.fillRect(bounds);

    const bool active = mNoteNumber >= 0;
    const bool inTune = active && std::abs(mSmoothedCents) < kInTuneCents;
    const auto colour = !active  ? theme::colours::textFaint
                        : inTune ? theme::colours::good
                                 : theme::colours::accent;

    // The ✕, drawn rather than a button so it matches the mock's weight.
    g.setColour(theme::colours::textFaint);
    g.drawLine(mCloseButton.getX() + 6.0f, mCloseButton.getY() + 6.0f, mCloseButton.getRight() - 6.0f,
               mCloseButton.getBottom() - 6.0f, 1.8f);
    g.drawLine(mCloseButton.getRight() - 6.0f, mCloseButton.getY() + 6.0f, mCloseButton.getX() + 6.0f,
               mCloseButton.getBottom() - 6.0f, 1.8f);

    drawNote(g, mNoteArea, colour);

    if (mMode == Mode::needle)
        drawCentsScale(g, mScaleArea, colour);
    else
        drawStrobe(g, mScaleArea, colour);

    drawStringPads(g, mPadsArea);
}

void TunerPanel::drawNote(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    if (mNoteNumber < 0)
    {
        g.setColour(theme::colours::textFaint);
        g.setFont(theme::fonts::ui(20.0f, 500));
        g.drawText(mLevel > 1.0e-4f ? "Listening..." : "Play a note", area,
                   juce::Justification::centred, false);
        return;
    }

    const auto name = juce::String(kNoteNames[((mNoteNumber % 12) + 12) % 12]);
    const auto octave = juce::String(mNoteNumber / 12 - 1);

    auto readout = area.removeFromBottom(28.0f);

    // Soft glow behind the letter, then the letter itself at 120px.
    const auto glow = area.withSizeKeepingCentre(area.getHeight() * 1.1f, area.getHeight() * 1.1f);
    juce::ColourGradient halo(colour.withAlpha(0.22f), glow.getCentreX(), glow.getCentreY(),
                              colour.withAlpha(0.0f), glow.getCentreX(), glow.getBottom(), true);
    g.setGradientFill(halo);
    g.fillEllipse(glow);

    const auto font = theme::fonts::ui(120.0f, 700);
    const auto nameWidth = juce::GlyphArrangement::getStringWidth(font, name);

    g.setColour(colour);
    g.setFont(font);
    g.drawText(name, area, juce::Justification::centred, false);

    // Octave as a small subscript to the right of the letter.
    g.setColour(theme::colours::textFaint);
    g.setFont(theme::fonts::mono(22.0f));
    g.drawText(octave,
               area.withX(area.getCentreX() + nameWidth * 0.5f + 8.0f)
                   .withWidth(40.0f)
                   .withTrimmedTop(area.getHeight() * 0.52f),
               juce::Justification::centredLeft, false);

    // "-2 cents · 82.3 Hz" in the note's colour: numerals, so mono.
    g.setColour(colour);
    g.setFont(theme::fonts::mono(15.0f));
    g.drawText(juce::String(juce::roundToInt(mSmoothedCents)) + " cents"
                   + juce::String::fromUTF8("  \xc2\xb7  ") + juce::String(mDisplayedFrequency, 1)
                   + " Hz",
               readout, juce::Justification::centred, false);
}

void TunerPanel::drawCentsScale(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto scale = area.withSizeKeepingCentre(kScaleWidth, area.getHeight());
    const auto ticksBottom = scale.getY() + 56.0f;
    const auto centsToX = [&scale](float cents) {
        return scale.getX() + scale.getWidth() * (cents + 50.0f) / 100.0f;
    };

    // The ±5 cent window: green wash with a brighter border.
    const juce::Rectangle<float> window(centsToX(-kInTuneCents), scale.getY() - 4.0f,
                                        centsToX(kInTuneCents) - centsToX(-kInTuneCents),
                                        ticksBottom - scale.getY() + 8.0f);
    g.setColour(theme::colours::good.withAlpha(0.1f));
    g.fillRoundedRectangle(window, 8.0f);
    g.setColour(theme::colours::good.withAlpha(0.3f));
    g.drawRoundedRectangle(window.reduced(0.5f), 8.0f, 1.0f);

    // Eleven ticks, the centre one heavier.
    for (int cents = -50; cents <= 50; cents += 10)
    {
        const auto x = centsToX(static_cast<float>(cents));
        const bool centre = cents == 0;

        g.setColour(centre ? theme::colours::outlineStrong : theme::colours::outline);
        g.fillRect(x - (centre ? 1.0f : 0.5f), scale.getY() + 8.0f, centre ? 2.0f : 1.0f,
                   ticksBottom - scale.getY() - 16.0f);
    }

    // Labels at the quarter points, which are not all on tick positions.
    for (const int cents : {-50, -25, 0, 25, 50})
    {
        g.setColour(theme::colours::textGhost);
        g.setFont(theme::fonts::mono(10.0f));
        g.drawText((cents > 0 ? "+" : "") + juce::String(cents),
                   juce::Rectangle<float>(centsToX(static_cast<float>(cents)) - 20.0f,
                                          ticksBottom + 4.0f, 40.0f, 14.0f),
                   juce::Justification::centred, false);
    }

    if (mNoteNumber < 0)
        return;

    // The needle: a 6px rounded bar in the note's colour, with its own glow.
    const auto x = centsToX(juce::jlimit(-50.0f, 50.0f, mSmoothedCents));
    const juce::Rectangle<float> needle(x - 3.0f, scale.getY(), 6.0f, ticksBottom - scale.getY());

    g.setColour(colour.withAlpha(0.35f));
    g.fillRoundedRectangle(needle.expanded(3.0f), 6.0f);
    g.setColour(colour);
    g.fillRoundedRectangle(needle, 3.0f);
}

void TunerPanel::drawStrobe(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto band = area.withSizeKeepingCentre(kScaleWidth, 56.0f);
    const bool active = mNoteNumber >= 0;

    // Bars scroll left when flat, right when sharp, freeze when in tune.
    constexpr int kBars = 14;
    const auto barWidth = band.getWidth() / kBars;
    const auto offset = static_cast<float>(std::fmod(mStrobePhase, 1.0)) * barWidth * 2.0f;

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle(band, theme::metrics::radiusMd);
    g.reduceClipRegion(clip);

    g.setColour(theme::colours::inset);
    g.fillRect(band);

    for (int i = -2; i < kBars + 2; i += 2)
    {
        const auto x = band.getX() + i * barWidth + offset;
        g.setColour(colour.withAlpha(active ? 0.55f : 0.12f));
        g.fillRect(x, band.getY(), barWidth, band.getHeight());
    }

    g.restoreState();

    g.setColour(theme::colours::outline);
    g.drawRoundedRectangle(band.reduced(0.5f), theme::metrics::radiusMd, 1.0f);
}

void TunerPanel::drawStringPads(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto count = static_cast<int>(kStringLabels.size());
    const auto gap = 10.0f;
    const auto totalWidth = count * kPadWidth + (count - 1) * gap;
    auto row = area.withSizeKeepingCentre(totalWidth, kPadHeight);
    const auto detected = detectedString();

    for (int i = 0; i < count; ++i)
    {
        const auto pad = row.removeFromLeft(kPadWidth);
        row.removeFromLeft(gap);

        const bool lit = i == detected;

        g.setColour(lit ? theme::colours::good.withAlpha(0.14f) : theme::colours::panel);
        g.fillRoundedRectangle(pad, theme::metrics::radiusMd);
        g.setColour(lit ? theme::colours::good : theme::colours::outline);
        g.drawRoundedRectangle(pad.reduced(0.5f), theme::metrics::radiusMd, lit ? 1.6f : 1.0f);

        g.setColour(lit ? theme::colours::good : theme::colours::textFaint);
        g.setFont(theme::fonts::ui(17.0f, 700));
        g.drawText(kStringLabels[static_cast<size_t>(i)], pad, juce::Justification::centred, false);
    }

    g.setColour(theme::colours::textGhost);
    g.setFont(theme::fonts::ui(11.0f));
    g.drawText("Output muted while tuning",
               area.withTrimmedLeft(area.getCentreX() - area.getX() + totalWidth * 0.5f + 24.0f),
               juce::Justification::centredLeft, false);
}

void TunerPanel::resized()
{
    auto area = getLocalBounds().reduced(theme::metrics::padding);

    auto top = area.removeFromTop(48);
    mSegmented.setBounds(top.removeFromLeft(220).withSizeKeepingCentre(220, 44));
    mCloseButton = top.removeFromRight(32).withSizeKeepingCentre(28.0f, 28.0f).toFloat();
    top.removeFromRight(12);
    mReference.setBounds(top.removeFromRight(120).withSizeKeepingCentre(120, 40));

    mPadsArea = area.removeFromBottom(72).toFloat();
    area.removeFromBottom(8);

    // Note and scale read as one centred group rather than drifting to the
    // edges of whatever space is left, which is what the mock shows.
    constexpr int kNoteHeight = 210;
    constexpr int kScaleHeight = 96;
    auto group = area.withSizeKeepingCentre(area.getWidth(), kNoteHeight + kScaleHeight);

    mNoteArea = group.removeFromTop(kNoteHeight).toFloat();
    mScaleArea = group.toFloat();
}

} // namespace blockrig
