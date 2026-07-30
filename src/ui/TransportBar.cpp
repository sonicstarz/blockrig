#include "ui/TransportBar.h"

#include "ui/Theme.h"

namespace blockrig
{
namespace
{
/// Signatures worth offering; anything stranger is rare enough to leave out.
const juce::StringArray kSignatures{"4/4", "3/4", "6/8", "5/4", "7/8", "12/8", "2/4", "9/8"};
} // namespace

TransportBar::TransportBar(BlockRigProcessor& processor)
    : mProcessor(processor)
{
    mBpmLabel.setText("BPM", juce::dontSendNotification);
    mBpmLabel.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    mBpmLabel.setColour(juce::Label::textColourId, theme::colours::textFaint);
    mBpmLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(mBpmLabel);

    mBpmValue.setFont(juce::FontOptions(17.0f, juce::Font::bold));
    mBpmValue.setJustificationType(juce::Justification::centredLeft);
    mBpmValue.setTooltip("Tempo for synced delays and modulation. Drag up and down to change, "
                         "shift-drag for fine control, double-click to type.");
    mBpmValue.onTextChange = [this] { applyBpmFromEditor(); };
    mBpmValue.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    mBpmValue.onDragStart = [this] { mDragBpm = mProcessor.getTransport().getBpm(); };
    mBpmValue.onDrag = [this](double delta) {
        mDragBpm += delta;
        mProcessor.getTransport().setBpm(mDragBpm);
        mShownBpm = 0.0; // force the readout to repaint even for small moves
        refresh();
    };
    addAndMakeVisible(mBpmValue);

    mTapButton.setTooltip("Tap the tempo. Two taps set it; keep tapping to steady it.");
    mTapButton.onClick = [this] {
        const auto taps = mProcessor.getTransport().tap();
        // Show that the tap registered even before there are enough for a tempo:
        // a button that does nothing visible for the first press reads as broken.
        mTapButton.setButtonText(taps < 2 ? "TAP…" : "TAP");
        mTapFlashCountdown = 8;
        mShownBpm = 0.0;
        refresh();
    };
    addAndMakeVisible(mTapButton);

    mTimeSignature.addItemList(kSignatures, 1);
    mTimeSignature.setTooltip("Time signature reported to plug-ins.");
    mTimeSignature.onChange = [this] {
        const auto text = mTimeSignature.getText();
        const auto parts = juce::StringArray::fromTokens(text, "/", "");

        if (parts.size() == 2)
            mProcessor.getTransport().setTimeSignature(parts[0].getIntValue(), parts[1].getIntValue());
    };
    addAndMakeVisible(mTimeSignature);

    refresh();
    startTimerHz(8);
}

TransportBar::~TransportBar()
{
    stopTimer();
}

void TransportBar::applyBpmFromEditor()
{
    const auto typed = mBpmValue.getText().getDoubleValue();

    if (typed > 0.0)
        mProcessor.getTransport().setBpm(typed);

    refresh();
}

void TransportBar::timerCallback()
{
    if (mTapFlashCountdown > 0 && --mTapFlashCountdown == 0)
        mTapButton.setButtonText("TAP");

    // Never stomp the readout while it is being typed into.
    if (mBpmValue.getCurrentTextEditor() == nullptr)
        refresh();
}

void TransportBar::refresh()
{
    auto& transport = mProcessor.getTransport();
    const bool following = transport.isFollowingHost();
    const auto bpm = transport.getBpm();

    if (std::abs(bpm - mShownBpm) > 0.05 || following != mWasFollowingHost)
    {
        mShownBpm = bpm;
        mBpmValue.setText(juce::String(bpm, bpm < 100.0 ? 2 : 1), juce::dontSendNotification);

        const auto signature = juce::String(transport.getTimeSignatureNumerator()) + "/"
                               + juce::String(transport.getTimeSignatureDenominator());
        const auto index = kSignatures.indexOf(signature);
        if (index >= 0)
            mTimeSignature.setSelectedItemIndex(index, juce::dontSendNotification);
    }

    if (following != mWasFollowingHost)
    {
        mWasFollowingHost = following;

        // The DAW is the authority when there is one; pretending otherwise would
        // let the user set a tempo that is silently overwritten every block.
        mBpmValue.setEditable(!following);
        mBpmValue.setColour(juce::Label::textColourId,
                            following ? theme::colours::textDim : theme::colours::text);
        mTapButton.setEnabled(!following);
        mTimeSignature.setEnabled(!following);
        mBpmLabel.setText(following ? "BPM (HOST)" : "BPM", juce::dontSendNotification);
        repaint();
    }

    if (mTapFlashCountdown > 0)
        repaint();
}

void TransportBar::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);

    g.setColour(theme::colours::panel);
    g.fillRoundedRectangle(bounds, theme::metrics::smallCornerRadius);
    g.setColour(mTapFlashCountdown > 0 ? theme::colours::accent : theme::colours::outline);
    g.drawRoundedRectangle(bounds, theme::metrics::smallCornerRadius, 1.0f);
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced(7, 3);

    mTimeSignature.setBounds(area.removeFromRight(62).withSizeKeepingCentre(62, 24));
    area.removeFromRight(6);
    mTapButton.setBounds(area.removeFromRight(42).withSizeKeepingCentre(42, 24));
    area.removeFromRight(8);

    mBpmLabel.setBounds(area.removeFromTop(11));
    mBpmValue.setBounds(area);
}

} // namespace blockrig
