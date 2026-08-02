#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "BlockRigProcessor.h"

namespace blockrig
{

/// The tuner (4i): needle or strobe, note name front and centre, cents scale
/// beneath, string pads along the bottom.
///
/// Fills the rig view rather than living in a window — tuning is the one thing
/// you do instead of everything else, and at stage distance the note has to be
/// enormous. Runs only while visible: showing it silences the rig's output (the
/// input keeps flowing to the detector); hiding it puts the output back.
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
    void mouseUp(const juce::MouseEvent&) override;

    /// The ✕ in the top-right.
    std::function<void()> onClose;

private:
    enum class Mode
    {
        needle,
        strobe
    };

    /// Needle / Strobe as one pill: active segment amber, dark text.
    class Segmented final : public juce::Component
    {
    public:
        std::function<void(int)> onSelect;

        void setSelected(int index)
        {
            mSelected = index;
            repaint();
        }

        void paint(juce::Graphics&) override;
        void mouseUp(const juce::MouseEvent&) override;

    private:
        int mSelected = 0;
    };

    void timerCallback() override;
    void drawNote(juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour);
    void drawCentsScale(juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour);
    void drawStrobe(juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour);
    void drawStringPads(juce::Graphics&, juce::Rectangle<float> area);

    /// Which of E A D G B e the detected note is nearest, or -1.
    int detectedString() const;

    BlockRigProcessor& mProcessor;

    Mode mMode = Mode::needle;
    Segmented mSegmented;
    juce::ComboBox mReference;
    juce::Rectangle<float> mCloseButton;

    /// Standard tuning, low to high, as MIDI note numbers.
    static constexpr std::array<int, 6> kStringNotes{40, 45, 50, 55, 59, 64};
    static constexpr std::array<const char*, 6> kStringLabels{"E", "A", "D", "G", "B", "e"};

    /// Inside this many cents reads as in tune (the mock's green zone).
    static constexpr float kInTuneCents = 5.0f;

    juce::Rectangle<float> mNoteArea, mScaleArea, mPadsArea;

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
