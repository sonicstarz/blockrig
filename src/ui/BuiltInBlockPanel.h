#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "blocks/eq/EqBlockProcessor.h"
#include "blocks/ir/IrBlockProcessor.h"
#include "blocks/utility/UtilityBlockProcessor.h"
#include "ui/Theme.h"

namespace blockrig
{

/// Editor for the IR block: which cabinet, and how much of it (4f).
///
/// Library / Open... / Clear live in the window's title bar; the body is the two
/// teal knobs and the impulse-response well, with the loaded file's name shown
/// as the window subtitle.
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

    juce::Component* getTitleBarRow() { return &mTitleBarRow; }
    int getTitleBarRowWidth() const { return mTitleBarRow.getPreferredWidth(); }
    juce::String getSubtitle() const { return mSubtitle; }
    std::function<void(const juce::String&)> onSubtitleChanged;

    static constexpr int kPreferredWidth = 620;
    static constexpr int kPreferredHeight = 146;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void showLibraryMenu();
    void chooseIr();
    void refresh();
    void loadWaveform();
    void setSubtitle(const juce::String& subtitle);

    IrBlockProcessor& mProcessor;

    theme::TitleBarRow mTitleBarRow;
    juce::TextButton mLibraryButton{"Library"}, mLoadButton{"Open..."}, mClearButton{"Clear"};
    juce::String mSubtitle;

    juce::Slider mMix, mOutput;
    juce::Label mMixLabel, mOutputLabel;
    std::unique_ptr<SliderAttachment> mMixAtt, mOutputAtt;

    /// Decimated to one signed peak per bucket, so the polyline keeps the
    /// early reflections' oscillation at any width.
    std::vector<float> mWaveform;
    juce::String mWaveformCaption;
    juce::Rectangle<float> mWell;

    std::unique_ptr<juce::FileChooser> mFileChooser;
    bool mDragHighlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrBlockPanel)
};

/// Editor for the utility block: gain, pan, and the phase switches.
class UtilityBlockPanel final : public juce::Component
{
public:
    explicit UtilityBlockPanel(UtilityBlockProcessor& processor);

    void resized() override;

    static constexpr int kPreferredWidth = 420;
    static constexpr int kPreferredHeight = 138;

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

/// Editor for the EQ block (4g): band chips in the title bar, an interactive
/// response graph, and Freq / Gain / Q for the selected band beneath it.
class EqBlockPanel final : public juce::Component
{
public:
    /// One row of the band table: which parameters a band has, and where its
    /// chip and handle get their identity.
    struct Band
    {
        const char* prefix;    ///< parameter id prefix ("hp", "b1", ...)
        const char* chipLabel; ///< "HP", "B1", ...
        bool hasGain;
        bool hasQ;
    };

    static constexpr std::array<Band, 6> kBands{{
        {"hp", "HP", false, false},
        {"ls", "LS", true, false},
        {"b1", "B1", true, true},
        {"b2", "B2", true, true},
        {"hs", "HS", true, false},
        {"lp", "LP", false, false},
    }};

    explicit EqBlockPanel(EqBlockProcessor& processor);

    void resized() override;

    juce::Component* getTitleBarRow();
    int getTitleBarRowWidth() const;

    static constexpr int kPreferredWidth = 660;
    static constexpr int kPreferredHeight = 300;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    /// The chip row lives in the window title bar: mono 11, selected chip
    /// solid blue with dark text, the rest outlined muted.
    class BandChipRow final : public juce::Component
    {
    public:
        std::function<void(int)> onSelect;
        void setSelected(int band);
        int getPreferredWidth() const;

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;

    private:
        int mSelected = 2; // B1, matching the mock's initial state

        static constexpr int kChipWidth = 34;
        static constexpr int kChipGap = 4;
    };

    /// The response graph: grid, curve, area fill, and one handle per band.
    /// Drag moves freq (and gain, where the band has one); the scroll wheel
    /// adjusts Q on the selected band; clicking a handle selects its band.
    class Graph final : public juce::Component
                      , private juce::Timer
    {
    public:
        Graph(EqBlockProcessor& processor);
        ~Graph() override;

        std::function<void(int)> onBandSelected;
        void setSelectedBand(int band);

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        void timerCallback() override;

        float frequencyToX(float frequency) const;
        float xToFrequency(float x) const;
        float decibelsToY(float decibels) const;

        /// Product of the active stages' magnitudes, mirroring the processor's
        /// coefficient construction exactly.
        float responseDb(float frequency) const;
        juce::Point<float> handleCentre(int band) const;

        void beginDrag(int band);
        void endDrag();

        EqBlockProcessor& mProcessor;
        int mSelected = 2;
        int mDragging = -1;
        std::array<float, 20> mLastSnapshot{};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Graph)
    };

    void selectBand(int band);

    EqBlockProcessor& mProcessor;

    BandChipRow mChips;
    Graph mGraph;

    juce::Slider mFreq, mGainKnob, mQ;
    juce::Label mFreqLabel, mGainLabel, mQLabel;
    juce::ToggleButton mBandOn{"Band on"};
    juce::Label mHint;

    std::unique_ptr<SliderAttachment> mFreqAtt, mGainAtt, mQAtt;
    std::unique_ptr<ButtonAttachment> mBandOnAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqBlockPanel)
};

} // namespace blockrig
