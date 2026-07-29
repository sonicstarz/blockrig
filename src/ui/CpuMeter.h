#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BlockRigProcessor.h"

namespace blockrig
{

/// Header CPU meter, and the per-block breakdown behind it.
///
/// Shows the fraction of the audio callback's time budget used — the same thing
/// every DAW meter shows, and *not* Activity Monitor CPU. That distinction is the
/// single most common source of user confusion in every forum thread on the
/// subject, so the breakdown panel says so explicitly.
///
/// The per-block table is the differentiating part: Reaper has per-FX numbers and
/// no guitar host does, while users of those hosts keep asking for them.
class CpuMeter final : public juce::Component
                     , public juce::SettableTooltipClient
                     , private juce::Timer
{
public:
    explicit CpuMeter(BlockRigProcessor& processor);
    ~CpuMeter() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void showBreakdown();

    BlockRigProcessor& mProcessor;

    float mDisplayed = 0.0f;
    float mPeakHold = 0.0f;
    int mPeakHoldCountdown = 0;
    int mDropouts = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CpuMeter)
};

} // namespace blockrig
