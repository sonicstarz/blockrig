#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BlockRigProcessor.h"

namespace blockrig
{

/// Tempo and time signature for the whole rig: what tempo-synced delays and
/// modulation follow.
///
/// Inside a DAW the host owns the tempo, so these go read-only and display what
/// the host reports rather than letting the user set a value that would be
/// silently overridden.
class TransportBar final : public juce::Component
                         , private juce::Timer
{
public:
    explicit TransportBar(BlockRigProcessor& processor);
    ~TransportBar() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();
    void applyBpmFromEditor();

    BlockRigProcessor& mProcessor;

    juce::Label mBpmLabel;
    juce::Label mBpmValue;   ///< editable
    juce::TextButton mTapButton{"TAP"};
    juce::ComboBox mTimeSignature;

    bool mWasFollowingHost = false;
    double mShownBpm = 0.0;
    int mTapFlashCountdown = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

} // namespace blockrig
