#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BlockRigProcessor.h"

namespace blockrig
{

/// The tuner: needle or strobe, note name front and centre, cents underneath.
///
/// Runs only while its window is open. Opening it silences the rig's output
/// (the input keeps flowing to the detector); closing it puts the output back.
/// Silencing lives in the processor rather than through the master mute, so
/// closing the tuner cannot accidentally unmute a rig the user had muted.
class TunerPanel final : public juce::Component
                       , private juce::Timer
{
public:
    explicit TunerPanel(BlockRigProcessor& processor);
    ~TunerPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kPreferredWidth = 460;
    static constexpr int kPreferredHeight = 320;

private:
    enum class Mode
    {
        needle,
        strobe
    };

    void timerCallback() override;
    void drawNeedle(juce::Graphics&, juce::Rectangle<float> area);
    void drawStrobe(juce::Graphics&, juce::Rectangle<float> area);

    BlockRigProcessor& mProcessor;

    Mode mMode = Mode::needle;
    juce::TextButton mNeedleButton{"Needle"};
    juce::TextButton mStrobeButton{"Strobe"};
    juce::ComboBox mReference;

    // Smoothed display state; raw YIN output flickers more than a tuner should.
    float mSmoothedCents = 0.0f;
    float mDisplayedFrequency = 0.0f;
    int mNoteNumber = -1;
    float mClarity = 0.0f;
    float mLevel = 0.0f;
    double mStrobePhase = 0.0;
    double mReferenceHz = 440.0;
    juce::int64 mLastSignalMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TunerPanel)
};

} // namespace blockrig
