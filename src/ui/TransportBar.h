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

    /// The BPM readout: drag vertically to change, double-click to type.
    ///
    /// Was a single-click-editable Label, which cannot drag - so the "drag or
    /// type" its tooltip promised half worked, and a single click swallowed the
    /// value into an editor when people expected to grab it.
    class BpmValue final : public juce::Label
    {
    public:
        BpmValue() { setEditable(false, true); }

        std::function<void(double deltaBpm)> onDrag;
        std::function<void()> onDragStart;

        void mouseDown(const juce::MouseEvent& event) override
        {
            mDragAccumulator = 0.0;
            if (onDragStart)
                onDragStart();
            juce::Label::mouseDown(event);
        }

        void mouseDrag(const juce::MouseEvent& event) override
        {
            // Slow vertical drag; fine control with shift.
            const auto scale = event.mods.isShiftDown() ? 0.02 : 0.25;
            const auto target = -event.getDistanceFromDragStartY() * scale;

            if (onDrag)
                onDrag(target - std::exchange(mDragAccumulator, static_cast<double>(target)));
        }

    private:
        double mDragAccumulator = 0.0;
    };

    juce::Label mBpmLabel;
    BpmValue mBpmValue;
    juce::TextButton mTapButton{"TAP"};
    juce::ComboBox mTimeSignature;

    bool mWasFollowingHost = false;
    double mShownBpm = 0.0;
    int mTapFlashCountdown = 0;
    double mDragBpm = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

} // namespace blockrig
