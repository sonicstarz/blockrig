#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "blocks/eq/EqBlockProcessor.h"
#include "blocks/ir/IrBlockProcessor.h"
#include "blocks/utility/UtilityBlockProcessor.h"

namespace blockrig
{

/// Editor for the IR block: which cabinet, and how much of it.
class IrBlockPanel final : public juce::Component
                         , public juce::FileDragAndDropTarget
{
public:
    explicit IrBlockPanel(IrBlockProcessor& processor);
    ~IrBlockPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;

    static constexpr int kPreferredWidth = 520;
    static constexpr int kPreferredHeight = 176;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void showLibraryMenu();
    void chooseIr();
    void refresh();

    IrBlockProcessor& mProcessor;

    juce::Label mIrName, mIrDetails;
    juce::TextButton mLibraryButton{"Library"}, mLoadButton{"Open..."}, mClearButton{"Clear"};

    juce::Slider mMix, mOutput;
    juce::Label mMixLabel, mOutputLabel;
    std::unique_ptr<SliderAttachment> mMixAtt, mOutputAtt;

    std::unique_ptr<juce::FileChooser> mFileChooser;
    bool mDragHighlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrBlockPanel)
};

/// Editor for the utility block: gain, pan, and the phase switches.
class UtilityBlockPanel final : public juce::Component
{
public:
    explicit UtilityBlockPanel(UtilityBlockProcessor& processor);

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kPreferredWidth = 420;
    static constexpr int kPreferredHeight = 150;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::Slider mGain, mPan;
    juce::Label mGainLabel, mPanLabel;
    juce::ToggleButton mInvertLeft{"Invert L"}, mInvertRight{"Invert R"}, mSwap{"Swap L/R"};

    std::unique_ptr<SliderAttachment> mGainAtt, mPanAtt;
    std::unique_ptr<ButtonAttachment> mInvertLeftAtt, mInvertRightAtt, mSwapAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UtilityBlockPanel)
};

/// Editor for the EQ block. Generated from the parameter list rather than laid
/// out by hand: five bands of the same three controls is exactly the case where
/// a generated grid beats bespoke placement.
class EqBlockPanel final : public juce::Component
{
public:
    explicit EqBlockPanel(juce::AudioProcessor& processor,
                          juce::AudioProcessorValueTreeState& state);

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kPreferredWidth = 640;
    static constexpr int kPreferredHeight = 260;

private:
    struct Control
    {
        juce::Label caption;
        std::unique_ptr<juce::Component> widget;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAtt;
        int column = 0;
        int row = 0;
    };

    void addControl(juce::AudioProcessorValueTreeState& state, const juce::String& id,
                    const juce::String& caption, int column, int row, bool isToggle);

    std::vector<std::unique_ptr<Control>> mControls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqBlockPanel)
};

} // namespace blockrig
