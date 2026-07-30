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
    const auto fall = [](float current, float target) { return target > current ? target : current * 0.85f; };

    // Fall slowly so a strummed note stays readable.
    mInput = fall(mInput, mProcessor.getInputLevel());
    mOutputLeft = fall(mOutputLeft, mProcessor.getOutputLevelLeft());
    mOutputRight = fall(mOutputRight, mProcessor.getOutputLevelRight());

    const auto loudest = juce::jmax(mOutputLeft, mOutputRight);
    const auto target = loudest > 0.0005f
                          ? juce::jlimit(0.0f, 1.0f, mProcessor.getStereoDifference() / loudest)
                          : 0.0f;
    mWidth = mWidth * 0.8f + target * 0.2f;

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
    const auto rowHeight = bounds.getHeight() / 4.0f;

    const auto drawRow = [&g](juce::Rectangle<float> row, const juce::String& label, float level) {
        g.setColour(theme::colours::textFaint);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(label, row.removeFromLeft(30.0f), juce::Justification::centredLeft, false);

        g.setColour(level > 0.0002f ? theme::colours::text : theme::colours::textFaint);
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(formatLevel(level), row.removeFromRight(32.0f), juce::Justification::centredRight, false);

        theme::drawLevelMeter(g, row.reduced(3.0f, row.getHeight() * 0.28f), level);
    };

    drawRow(bounds.removeFromTop(rowHeight), "IN", mInput);
    drawRow(bounds.removeFromTop(rowHeight), "OUT L", mOutputLeft);
    drawRow(bounds.removeFromTop(rowHeight), "OUT R", mOutputRight);

    auto width = bounds;
    g.setColour(theme::colours::textFaint);
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText("WIDTH", width.removeFromLeft(30.0f), juce::Justification::centredLeft, false);

    const bool mono = mWidth < 0.01f;
    g.setColour(mono ? theme::colours::textFaint : theme::colours::good);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText(mono ? "mono" : juce::String(juce::roundToInt(mWidth * 100.0f)) + "%",
               width.removeFromRight(32.0f), juce::Justification::centredRight, false);

    theme::drawLevelMeter(g, width.reduced(3.0f, width.getHeight() * 0.28f), mWidth);
}

} // namespace blockrig
