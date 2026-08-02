#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "BlockRigProcessor.h"

namespace blockrig
{

/// The performance surface (4j): what a rig looks like from standing height.
///
/// Everything here is either huge or absent. Nothing is editable — a stage is
/// where you press things, not where you dial them — so a mis-hit costs you a
/// wrong scene, never a lost setting.
///
/// The setlist column maps songs onto rigs and sections onto scenes: the song
/// is the rig you are on, its sections are that rig's scenes in order, and the
/// one you are playing is the active scene.
class GigView final : public juce::Component
                    , private juce::Timer
{
public:
    explicit GigView(BlockRigProcessor& processor);
    ~GigView() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;

    /// Set by MainView before it is shown.
    juce::String rigName;
    juce::String setlistName;
    int setlistIndex = -1; ///< 1-based position of this rig in the set
    int setlistCount = 0;

    std::function<void()> onExit;
    std::function<void()> onToggleTuner;
    std::function<void(int direction)> onStepRig;

    void refresh();

private:
    /// One scene pad: letter, name, and the category dots of the blocks the
    /// scene touches.
    class ScenePad final : public juce::Component
    {
    public:
        ScenePad(int index, juce::String name, juce::Array<juce::Colour> dots);

        void setActive(bool isActive);
        void setContent(juce::String name, juce::Array<juce::Colour> dots);

        std::function<void()> onClick;

        void paint(juce::Graphics&) override;
        void mouseUp(const juce::MouseEvent&) override;

    private:
        int mIndex;
        juce::String mName;
        juce::Array<juce::Colour> mDots;
        bool mActive = false;
    };

    /// The dashed "+" pad: captures the rig as it currently sounds.
    class AddPad final : public juce::Component
    {
    public:
        std::function<void()> onClick;

        void paint(juce::Graphics&) override;
        void mouseUp(const juce::MouseEvent&) override;
    };

    void timerCallback() override;
    void paintHeader(juce::Graphics&, juce::Rectangle<float> header);
    void paintSetlistColumn(juce::Graphics&, juce::Rectangle<float> column);

    /// Up to five category colours for the blocks a scene saved.
    juce::Array<juce::Colour> dotsForScene(size_t index) const;

    BlockRigProcessor& mProcessor;

    std::vector<std::unique_ptr<ScenePad>> mScenePads;
    AddPad mAddPad;
    juce::TextButton mTuner{"Tuner"}, mExit{"Exit"};
    juce::TextButton mPrevSong{"<< Prev song"}, mNextSong{"Next song >>"};

    /// Section rows are painted, not components; this is where they landed.
    std::vector<juce::Rectangle<float>> mSectionRows;
    juce::Rectangle<float> mColumnArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GigView)
};

} // namespace blockrig
