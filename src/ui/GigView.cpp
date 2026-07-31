#include "ui/GigView.h"

#include "ui/Theme.h"

namespace blockrig
{
namespace
{
const char* kNoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
} // namespace

/// A stage button: big, flat, and obviously pressable from three metres away.
class GigView::BigButton final : public juce::Component
{
public:
    BigButton(juce::String label, juce::Colour colour)
        : mLabel(std::move(label))
        , mColour(colour)
    {
    }

    void setLabel(juce::String label)
    {
        mLabel = std::move(label);
        repaint();
    }

    void setActive(bool isActive)
    {
        if (mActive == isActive)
            return;

        mActive = isActive;
        repaint();
    }

    void setColour(juce::Colour colour)
    {
        mColour = colour;
        repaint();
    }

    std::function<void()> onClick;

    void mouseDown(const juce::MouseEvent&) override
    {
        mDown = true;
        repaint();
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        mDown = false;
        repaint();

        if (contains(event.getPosition()) && onClick)
            onClick();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(4.0f);
        const auto radius = theme::metrics::cornerRadius * 1.4f;

        g.setColour(mActive ? mColour.withAlpha(0.9f)
                            : theme::colours::panelRaised.withAlpha(mDown ? 1.0f : 0.85f));
        g.fillRoundedRectangle(bounds, radius);

        g.setColour(mActive ? mColour.brighter(0.3f) : theme::colours::outlineStrong);
        g.drawRoundedRectangle(bounds, radius, mActive ? 3.0f : 1.6f);

        // Scale the type to the button, so one component serves both the huge
        // snapshot buttons and the smaller transport row.
        const auto fontSize = juce::jlimit(15.0f, 34.0f, bounds.getHeight() * 0.28f);

        g.setColour(mActive ? juce::Colours::black.withAlpha(0.85f) : theme::colours::text);
        g.setFont(juce::FontOptions(fontSize, juce::Font::bold));
        g.drawFittedText(mLabel, bounds.toNearestInt().reduced(10, 6), juce::Justification::centred, 2,
                         0.7f);
    }

private:
    juce::String mLabel;
    juce::Colour mColour;
    bool mActive = false;
    bool mDown = false;
};

//==============================================================================
GigView::GigView(BlockRigProcessor& processor)
    : mProcessor(processor)
{
    const auto add = [this](std::unique_ptr<BigButton>& target, juce::String label,
                            juce::Colour colour, std::function<void()> action) {
        target = std::make_unique<BigButton>(std::move(label), colour);
        target->onClick = std::move(action);
        addAndMakeVisible(*target);
    };

    add(mPrevRig, "<", theme::colours::accent, [this] {
        if (onStepRig)
            onStepRig(-1);
    });
    add(mNextRig, ">", theme::colours::accent, [this] {
        if (onStepRig)
            onStepRig(1);
    });
    add(mMute, "MUTE", theme::colours::bad, [this] {
        mProcessor.setMuted(!mProcessor.isMuted());
        refresh();
    });
    add(mTuner, "TUNER", theme::colours::good, [this] {
        mProcessor.setTunerActive(!mProcessor.isTunerActive());
        refresh();
    });
    add(mTap, "TAP", theme::colours::accent, [this] { mProcessor.getTransport().tap(); });
    add(mExit, "EXIT", theme::colours::textFaint, [this] {
        if (onExit)
            onExit();
    });

    refresh();
    startTimerHz(15);
}

GigView::~GigView()
{
    stopTimer();
}

void GigView::refresh()
{
    const auto& snapshots = mProcessor.getSnapshots().getSnapshots();

    if (mSnapshotButtons.size() != snapshots.size())
    {
        mSnapshotButtons.clear();

        for (size_t i = 0; i < snapshots.size(); ++i)
        {
            auto button = std::make_unique<BigButton>(snapshots[i].name, theme::colours::accent);
            const auto index = static_cast<int>(i);

            button->onClick = [this, index] {
                auto& bank = mProcessor.getSnapshots();

                if (index < static_cast<int>(bank.getSnapshots().size()))
                {
                    snapshots::Bank::apply(mProcessor, bank.getSnapshots()[static_cast<size_t>(index)]);
                    bank.activeIndex = index;
                    refresh();
                }
            };

            addAndMakeVisible(*button);
            mSnapshotButtons.push_back(std::move(button));
        }

        resized();
    }

    const auto active = mProcessor.getSnapshots().activeIndex;

    for (size_t i = 0; i < mSnapshotButtons.size(); ++i)
    {
        mSnapshotButtons[i]->setLabel(snapshots[i].name);
        mSnapshotButtons[i]->setActive(static_cast<int>(i) == active);
    }

    mMute->setActive(mProcessor.isMuted());
    mTuner->setActive(mProcessor.isTunerActive());
    repaint();
}

void GigView::timerCallback()
{
    refresh();

    if (!mProcessor.isTunerActive())
        return;

    const auto result = mProcessor.getPitchDetector().analyse();

    if (result.frequency > 20.0f && result.clarity > 0.5f)
    {
        mTunerFrequency = result.frequency;
        const auto midi = 69.0 + 12.0 * std::log2(result.frequency / 440.0);
        mTunerNote = static_cast<int>(std::round(midi));
        mTunerCents = mTunerCents * 0.7f + static_cast<float>((midi - mTunerNote) * 100.0) * 0.3f;
    }
}

void GigView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    auto header = getLocalBounds().removeFromTop(96);

    // The rig's name, as large as the space allows: the one thing you check
    // between songs.
    g.setColour(theme::colours::text);
    g.setFont(juce::FontOptions(42.0f, juce::Font::bold));
    g.drawFittedText(rigName, header.reduced(160, 10), juce::Justification::centred, 1, 0.6f);

    if (!mProcessor.isTunerActive())
        return;

    // Tuner takes over the snapshot area while it is up; nobody presses scenes
    // and tunes at the same time.
    auto area = getLocalBounds().withTrimmedTop(96).withTrimmedBottom(120);
    g.setColour(juce::Colours::black);
    g.fillRect(area);

    const bool inTune = mTunerNote >= 0 && std::abs(mTunerCents) < 3.0f;
    const auto accent = inTune ? theme::colours::good : theme::colours::accent;

    if (mTunerNote >= 0)
    {
        g.setColour(accent);
        g.setFont(juce::FontOptions(juce::jmin(180.0f, area.getHeight() * 0.55f), juce::Font::bold));
        g.drawText(juce::String(kNoteNames[((mTunerNote % 12) + 12) % 12]),
                   area.removeFromTop(area.getHeight() * 0.7f), juce::Justification::centred, false);

        // A wide bar, because a needle drawn thin is invisible past two metres.
        auto meter = area.reduced(area.getWidth() / 6, 0).removeFromTop(40).toFloat();
        g.setColour(theme::colours::outlineStrong);
        g.fillRoundedRectangle(meter, 6.0f);

        const auto clamped = juce::jlimit(-50.0f, 50.0f, mTunerCents);
        const auto centreX = meter.getCentreX();
        const auto travel = meter.getWidth() * 0.5f * (clamped / 50.0f);

        g.setColour(accent);
        g.fillRoundedRectangle(juce::Rectangle<float>(juce::jmin(centreX, centreX + travel),
                                                      meter.getY(), std::abs(travel), meter.getHeight()),
                               6.0f);

        g.setColour(theme::colours::text);
        g.fillRect(centreX - 2.0f, meter.getY() - 6.0f, 4.0f, meter.getHeight() + 12.0f);
    }
    else
    {
        g.setColour(theme::colours::textFaint);
        g.setFont(juce::FontOptions(28.0f));
        g.drawText("Play a note", area, juce::Justification::centred, false);
    }
}

void GigView::resized()
{
    auto area = getLocalBounds();

    // Header: rig stepping either side of the name.
    auto header = area.removeFromTop(96);
    mPrevRig->setBounds(header.removeFromLeft(96));
    mNextRig->setBounds(header.removeFromRight(96));

    // Footer: the transport controls, always in the same place.
    auto footer = area.removeFromBottom(120);
    const auto footerWidth = footer.getWidth() / 4;
    mMute->setBounds(footer.removeFromLeft(footerWidth));
    mTuner->setBounds(footer.removeFromLeft(footerWidth));
    mTap->setBounds(footer.removeFromLeft(footerWidth));
    mExit->setBounds(footer);

    if (mSnapshotButtons.empty())
        return;

    // Snapshots fill what is left, in as few rows as keeps them chunky.
    const auto count = static_cast<int>(mSnapshotButtons.size());
    const auto columns = juce::jlimit(1, 4, static_cast<int>(std::ceil(std::sqrt(count))));
    const auto rows = (count + columns - 1) / columns;

    const auto cellWidth = area.getWidth() / columns;
    const auto cellHeight = area.getHeight() / juce::jmax(1, rows);

    for (int i = 0; i < count; ++i)
    {
        const auto column = i % columns;
        const auto row = i / columns;

        mSnapshotButtons[static_cast<size_t>(i)]->setBounds(area.getX() + column * cellWidth,
                                                            area.getY() + row * cellHeight, cellWidth,
                                                            cellHeight);
    }
}

} // namespace blockrig
