#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BlockRigProcessor.h"

namespace blockrig
{

/// Always-visible IN and OUT meters with numeric dBFS readouts.
///
/// Exists because "is my guitar even reaching this thing?" is the first question
/// anyone asks, and a small bar tucked inside a block tile does not answer it.
/// The numbers matter as much as the bars: they distinguish "silent" from "quiet
/// but present", which is exactly the confusion a bar alone creates.
class HeaderMeters final : public juce::Component
                         , private juce::Timer
{
public:
    explicit HeaderMeters(BlockRigProcessor& processor);
    ~HeaderMeters() override;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    static juce::String formatLevel(float linearLevel);

    BlockRigProcessor& mProcessor;
    float mInput = 0.0f;
    float mOutputLeft = 0.0f;
    float mOutputRight = 0.0f;
    /// How different the two output channels are, as a fraction of their level.
    /// A number, because "is this actually stereo" is not a question a pair of
    /// bars can answer - two identical bars look exactly like a wide mix.
    float mWidth = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeaderMeters)
};

} // namespace blockrig
