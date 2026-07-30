#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "blocks/nam/NamBlockProcessor.h"

namespace blockrig
{

/// Inline editor for the built-in NAM block.
///
/// Built-ins edit here rather than in a floating window: it is the showcase
/// surface of the app, and it follows the GENOME / Neural DSP pattern of a chain
/// strip above and the selected block's controls below.
class NamBlockPanel final : public juce::Component
                          , public juce::FileDragAndDropTarget
                          , private juce::Timer
{
public:
    explicit NamBlockPanel(NamBlockProcessor& processor);
    ~NamBlockPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void refreshCaptureInfo();
    void chooseCapture();
    void addKnob(juce::Slider&, juce::Label&, const juce::String& caption, const char* paramId,
                 std::unique_ptr<SliderAttachment>&);
    void showLibraryMenu();

    NamBlockProcessor& mProcessor;

    juce::Label mCaptureName, mCaptureDetails;
    /// The library is the primary way to pick a capture once it has content;
    /// the file chooser feeds it.
    juce::TextButton mLibraryButton{"Library"};
    juce::TextButton mLoadButton{"Open..."};
    juce::TextButton mClearButton{"Clear"};

    juce::Slider mInTrim, mBass, mMid, mTreble, mOutTrim, mCalDbu, mSlim, mGateThreshold;
    juce::Label mInTrimLabel, mBassLabel, mMidLabel, mTrebleLabel, mOutTrimLabel, mCalDbuLabel, mSlimLabel,
        mGateThresholdLabel;

    juce::ToggleButton mEqOn{"EQ"}, mGateOn{"Gate"}, mCalibrateInput{"Calibrate input"},
        mStereo{"True stereo"};
    juce::ComboBox mOutputMode;
    juce::Label mOutputModeLabel;

    std::unique_ptr<SliderAttachment> mInTrimAtt, mBassAtt, mMidAtt, mTrebleAtt, mOutTrimAtt, mCalDbuAtt, mSlimAtt,
        mGateThresholdAtt;
    std::unique_ptr<ButtonAttachment> mEqOnAtt, mGateOnAtt, mCalibrateInputAtt, mStereoAtt;
    std::unique_ptr<ComboAttachment> mOutputModeAtt;

    std::unique_ptr<juce::FileChooser> mFileChooser;
    /// Laid out in resized(), drawn in paint() as the grid's dividers.
    juce::Array<juce::Rectangle<int>> mKnobCells;
    bool mDragHighlight = false;
    float mInputLevel = 0.0f, mOutputLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NamBlockPanel)
};

} // namespace blockrig
