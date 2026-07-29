#include "ui/CpuMeter.h"

#include <algorithm>
#include <vector>

#include "ui/Theme.h"

namespace blockrig
{
namespace
{
constexpr float kWarnThreshold = 0.7f;
constexpr int kPeakHoldTicks = 22; // roughly 1.5 s at 15 Hz
} // namespace

CpuMeter::CpuMeter(BlockRigProcessor& processor)
    : mProcessor(processor)
{
    setTooltip("Share of the audio buffer's time budget. Click for a per-block breakdown.");
    startTimerHz(15);
}

CpuMeter::~CpuMeter()
{
    stopTimer();
}

void CpuMeter::timerCallback()
{
    auto& load = mProcessor.getChain().getTotalLoad();
    mDisplayed = load.getAverage();

    // Hold the highest spike briefly, then let it fall back, so the tick tracks
    // recent behaviour rather than latching on the worst block since launch.
    const auto peak = load.getPeak();

    if (peak >= mPeakHold || mPeakHoldCountdown <= 0)
    {
        mPeakHold = peak;
        mPeakHoldCountdown = kPeakHoldTicks;
    }
    else
    {
        --mPeakHoldCountdown;
    }

    load.clearPeak();

    mDropouts = mProcessor.getChain().getDropoutCount();
    repaint();
}

void CpuMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Numeric read-out on the right, bar on the left.
    auto textArea = bounds.removeFromRight(46.0f);
    auto barArea = bounds.reduced(0.0f, 7.0f);

    g.setColour(theme::colours::background);
    g.fillRoundedRectangle(barArea, 3.0f);

    const auto clamped = juce::jlimit(0.0f, 1.0f, mDisplayed);
    const auto colour = mDropouts > 0 ? theme::colours::bad
                        : clamped > kWarnThreshold ? theme::colours::warn
                                                   : theme::colours::meterLow;

    if (clamped > 0.001f)
    {
        g.setColour(colour);
        g.fillRoundedRectangle(barArea.withWidth(barArea.getWidth() * clamped), 3.0f);
    }

    // Peak-hold tick: averages hide the spikes that actually cause dropouts.
    const auto peak = juce::jlimit(0.0f, 1.0f, mPeakHold);
    if (peak > 0.01f)
    {
        const auto x = barArea.getX() + barArea.getWidth() * peak;
        g.setColour(theme::colours::text.withAlpha(0.75f));
        g.fillRect(x - 1.0f, barArea.getY(), 1.5f, barArea.getHeight());
    }

    g.setColour(mDropouts > 0 ? theme::colours::bad : theme::colours::textDim);
    g.setFont(juce::FontOptions(11.5f));
    g.drawText(juce::String(juce::roundToInt(clamped * 100.0f)) + "%", textArea,
               juce::Justification::centredRight, false);

    if (mDropouts > 0)
    {
        g.setColour(theme::colours::bad);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(juce::String(mDropouts) + " drop", getLocalBounds().toFloat().removeFromBottom(10.0f),
                   juce::Justification::centredRight, false);
    }
}

void CpuMeter::mouseDown(const juce::MouseEvent&)
{
    showBreakdown();
}

void CpuMeter::showBreakdown()
{
    juce::PopupMenu menu;

    const auto& total = mProcessor.getChain().getTotalLoad();
    menu.addSectionHeader("Total " + juce::String(total.getAverage() * 100.0f, 1) + "% of the buffer budget");

    const auto blocks = mProcessor.getChain().getBlocks();

    if (blocks.empty())
    {
        menu.addItem(juce::PopupMenu::Item("No blocks in the chain").setEnabled(false));
    }
    else
    {
        // Heaviest first: the point of this panel is finding the one offender.
        std::vector<BlockInstance*> sorted(blocks.begin(), blocks.end());
        std::sort(sorted.begin(), sorted.end(), [](const BlockInstance* a, const BlockInstance* b) {
            return a->getLoad().getPeak() > b->getLoad().getPeak();
        });

        for (auto* block : sorted)
        {
            const auto& load = block->getLoad();
            const auto text = block->getDisplayName().paddedRight(' ', 20) + "  avg "
                              + juce::String(load.getAverage() * 100.0f, 2) + "%   peak "
                              + juce::String(load.getPeak() * 100.0f, 2) + "%   "
                              + juce::String(block->getLatencySamples()) + " smp";
            menu.addItem(juce::PopupMenu::Item(text).setEnabled(false));
        }
    }

    menu.addSeparator();

    if (mDropouts > 0)
    {
        menu.addItem(1, "Clear " + juce::String(mDropouts) + " dropout warning"
                            + (mDropouts == 1 ? "" : "s"));
        menu.addSeparator();
    }

    menu.addItem(juce::PopupMenu::Item("This is the audio callback's time budget,").setEnabled(false));
    menu.addItem(juce::PopupMenu::Item("not Activity Monitor CPU. 100% = dropouts.").setEnabled(false));

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int choice) {
        if (choice == 1)
            mProcessor.getChain().clearDropoutCount();
    });
}

} // namespace blockrig
