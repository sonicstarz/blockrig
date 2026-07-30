#include "ui/HeaderMeters.h"

#include "ui/Theme.h"

namespace blockrig
{

HeaderMeters::HeaderMeters(BlockRigProcessor& processor)
    : mProcessor(processor)
{
    setInterceptsMouseClicks(false, false);
    startTimerHz(20);
}

HeaderMeters::~HeaderMeters()
{
    stopTimer();
}

void HeaderMeters::timerCallback()
{
    const auto input = mProcessor.getInputLevel();
    const auto output = mProcessor.getOutputLevel();

    // Fall slowly so a strummed note stays readable.
    mInput = input > mInput ? input : mInput * 0.85f;
    mOutput = output > mOutput ? output : mOutput * 0.85f;

    repaint();
}

juce::String HeaderMeters::formatLevel(float linearLevel)
{
    if (linearLevel <= 0.0002f)
        return "--";

    return juce::String(juce::Decibels::gainToDecibels(linearLevel), 1);
}

void HeaderMeters::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto rowHeight = bounds.getHeight() * 0.5f;

    const auto drawRow = [&g](juce::Rectangle<float> row, const juce::String& label, float level) {
        g.setColour(theme::colours::textFaint);
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.drawText(label, row.removeFromLeft(24.0f), juce::Justification::centredLeft, false);

        // Numeric readout: the part that says "quiet but present" rather than
        // leaving the user guessing at a short bar.
        g.setColour(level > 0.0002f ? theme::colours::text : theme::colours::textFaint);
        g.setFont(juce::FontOptions(10.5f));
        g.drawText(formatLevel(level), row.removeFromRight(34.0f), juce::Justification::centredRight, false);

        theme::drawLevelMeter(g, row.reduced(3.0f, row.getHeight() * 0.3f), level);
    };

    drawRow(bounds.removeFromTop(rowHeight), "IN", mInput);
    drawRow(bounds, "OUT", mOutput);
}

} // namespace blockrig
