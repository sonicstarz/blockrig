#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "blocks/nam/NamBlockProcessor.h"
#include "ui/Theme.h"

namespace blockrig
{

/// Editor for the built-in NAM block (4c): Library / Open... / Clear and the
/// EQ / Gate / Stereo switches live in the window's title bar next to the
/// capture filename; the body is the amber knob row plus the output-mode
/// column, with the capture's metadata as a faint ribbon along the bottom.
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

    juce::Component* getTitleBarRow() { return &mTitleBarRow; }
    int getTitleBarRowWidth() const { return mTitleBarRow.getPreferredWidth(); }
    juce::String getSubtitle() const { return mSubtitle; }
    std::function<void(const juce::String&)> onSubtitleChanged;

    static constexpr int kPreferredWidth = 944;
    static constexpr int kPreferredHeight = 152;

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
    void setSubtitle(const juce::String& subtitle);

    NamBlockProcessor& mProcessor;

    theme::TitleBarRow mTitleBarRow;
    juce::TextButton mLibraryButton{"Library"};
    juce::TextButton mLoadButton{"Open..."};
    juce::TextButton mClearButton{"Clear"};
    juce::String mSubtitle;

    juce::Slider mInTrim, mBass, mMid, mTreble, mOutTrim, mCalDbu, mSlim, mGateThreshold;
    juce::Label mInTrimLabel, mBassLabel, mMidLabel, mTrebleLabel, mOutTrimLabel, mCalDbuLabel, mSlimLabel,
        mGateThresholdLabel;

    juce::ToggleButton mEqOn{"EQ"}, mGateOn{"Gate"}, mCalibrateInput{"Calibrate input"},
        mStereo{"Stereo"};
    juce::ComboBox mOutputMode;
    juce::Label mOutputModeLabel;

    std::unique_ptr<SliderAttachment> mInTrimAtt, mBassAtt, mMidAtt, mTrebleAtt, mOutTrimAtt, mCalDbuAtt, mSlimAtt,
        mGateThresholdAtt;
    std::unique_ptr<ButtonAttachment> mEqOnAtt, mGateOnAtt, mCalibrateInputAtt, mStereoAtt;
    std::unique_ptr<ComboAttachment> mOutputModeAtt;

    std::unique_ptr<juce::FileChooser> mFileChooser;

    /// The capture's metadata (sample rate, loudness, calibration levels),
    /// drawn as a faint mono ribbon along the bottom of the body.
    juce::String mDetails;

    juce::Rectangle<float> mInMeter, mOutMeter;
    juce::Rectangle<int> mDetailsArea;
    float mDividerX = 0.0f;

    bool mDragHighlight = false;
    float mInputLevel = 0.0f, mOutputLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NamBlockPanel)
};

} // namespace blockrig
