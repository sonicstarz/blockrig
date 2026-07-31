#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "BlockRigProcessor.h"

namespace blockrig
{

/// The performance surface: what a rig looks like from standing height.
///
/// Everything here is either huge or absent. Nothing is editable — a stage is
/// where you press things, not where you dial them — so a mis-hit costs you a
/// wrong snapshot, never a lost setting.
class GigView final : public juce::Component
                    , private juce::Timer
{
public:
    explicit GigView(BlockRigProcessor& processor);
    ~GigView() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /// Set by MainView: the rig's name, and how to leave.
    juce::String rigName;
    std::function<void()> onExit;
    std::function<void(int direction)> onStepRig;

    void refresh();

private:
    class BigButton;

    void timerCallback() override;

    BlockRigProcessor& mProcessor;

    std::vector<std::unique_ptr<BigButton>> mSnapshotButtons;
    std::unique_ptr<BigButton> mPrevRig, mNextRig, mMute, mTuner, mTap, mExit;

    /// Tuner readout, big enough to read while standing.
    int mTunerNote = -1;
    float mTunerCents = 0.0f;
    float mTunerFrequency = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GigView)
};

} // namespace blockrig
