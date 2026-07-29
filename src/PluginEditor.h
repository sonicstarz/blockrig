#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

namespace nammodeler
{

/// One amp channel's controls: model slot plus its trims, tone stack, and the
/// NAM-derived output mode / calibration / model-size settings.
class AmpSlotPanel : public juce::Component
                   , public juce::FileDragAndDropTarget
{
public:
    AmpSlotPanel(NAMModelerProcessor& processor, int slotIndex);

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    /// Refreshes the model name, badges and which controls are available.
    void refreshModelState();

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void attachSlider(juce::Slider& slider, juce::Label& label, const juce::String& text, const char* paramId,
                      std::unique_ptr<SliderAttachment>& attachment);
    void attachButton(juce::Button& button, const char* paramId, std::unique_ptr<ButtonAttachment>& attachment);
    void chooseModel();

    NAMModelerProcessor& mProcessor;
    const int mSlotIndex;

    juce::Label mTitle;
    juce::TextButton mLoadButton{"Load .nam"};
    juce::TextButton mClearButton{"X"};
    juce::Label mModelName;
    juce::Label mModelDetails;

    juce::ToggleButton mEnabled{"On"};
    juce::ToggleButton mPhase{"Ø"};
    juce::ToggleButton mSolo{"Solo"};
    juce::ToggleButton mMute{"Mute"};
    juce::ToggleButton mEqOn{"EQ"};
    juce::ToggleButton mCalibrateInput{"Calibrate Input"};

    juce::Slider mInTrim, mOutTrim, mPan, mBass, mMid, mTreble, mCalDbu, mSlim;
    juce::Label mInTrimLabel, mOutTrimLabel, mPanLabel, mBassLabel, mMidLabel, mTrebleLabel, mCalDbuLabel,
        mSlimLabel;

    juce::ComboBox mOutputMode;
    juce::Label mOutputModeLabel;

    std::unique_ptr<SliderAttachment> mInTrimAttachment, mOutTrimAttachment, mPanAttachment, mBassAttachment,
        mMidAttachment, mTrebleAttachment, mCalDbuAttachment, mSlimAttachment;
    std::unique_ptr<ButtonAttachment> mEnabledAttachment, mPhaseAttachment, mSoloAttachment, mMuteAttachment,
        mEqOnAttachment, mCalibrateInputAttachment;
    std::unique_ptr<ComboAttachment> mOutputModeAttachment;

    std::unique_ptr<juce::FileChooser> mFileChooser;
    bool mDragHighlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpSlotPanel)
};

class NAMModelerEditor : public juce::AudioProcessorEditor
                       , private juce::Timer
{
public:
    explicit NAMModelerEditor(NAMModelerProcessor&);
    ~NAMModelerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    NAMModelerProcessor& mProcessor;

    std::array<std::unique_ptr<AmpSlotPanel>, NAMModelerProcessor::kNumSlots> mSlotPanels;

    juce::ComboBox mInputMode;
    juce::Label mInputModeLabel;
    juce::Slider mMasterOut;
    juce::Label mMasterOutLabel;
    juce::ToggleButton mMonoSum{"Mono Sum"};
    juce::ToggleButton mGateOn{"Gate"};
    juce::Slider mGateThreshold;
    juce::Label mGateThresholdLabel;
    juce::Label mLatencyLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mInputModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mMasterOutAttachment,
        mGateThresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mMonoSumAttachment, mGateOnAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NAMModelerEditor)
};

} // namespace nammodeler
