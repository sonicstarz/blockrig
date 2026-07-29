#include "ui/Theme.h"

namespace blockrig::theme
{

Look::Look()
{
    setColour(juce::ResizableWindow::backgroundColourId, colours::background);
    setColour(juce::Label::textColourId, colours::text);

    setColour(juce::Slider::rotarySliderFillColourId, colours::accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, colours::outline);
    setColour(juce::Slider::textBoxTextColourId, colours::textDim);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);

    setColour(juce::TextButton::buttonColourId, colours::panelRaised);
    setColour(juce::TextButton::buttonOnColourId, colours::accentDim);
    setColour(juce::TextButton::textColourOffId, colours::text);
    setColour(juce::TextButton::textColourOnId, colours::text);

    setColour(juce::ComboBox::backgroundColourId, colours::panelRaised);
    setColour(juce::ComboBox::outlineColourId, colours::outline);
    setColour(juce::ComboBox::textColourId, colours::text);
    setColour(juce::ComboBox::arrowColourId, colours::textDim);

    setColour(juce::PopupMenu::backgroundColourId, colours::panelRaised);
    setColour(juce::PopupMenu::textColourId, colours::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colours::accentDim);
    setColour(juce::PopupMenu::highlightedTextColourId, colours::text);

    setColour(juce::TextEditor::backgroundColourId, colours::panel);
    setColour(juce::TextEditor::outlineColourId, colours::outline);
    setColour(juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour(juce::TextEditor::textColourId, colours::text);
    setColour(juce::TextEditor::highlightColourId, colours::accentDim);

    setColour(juce::ScrollBar::thumbColourId, colours::outlineStrong);
    setColour(juce::TooltipWindow::backgroundColourId, colours::panelRaised);
    setColour(juce::TooltipWindow::textColourId, colours::text);
    setColour(juce::TooltipWindow::outlineColourId, colours::outline);
}

void Look::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                            float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(3.0f);
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto lineWidth = juce::jmax(2.5f, radius * 0.14f);
    const auto arcRadius = radius - lineWidth * 0.5f;

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(colours::outline);
    g.strokePath(track, juce::PathStrokeType(lineWidth, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    if (slider.isEnabled())
    {
        // Bipolar controls read better filled from the centre outwards.
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const auto startAngle =
            bipolar ? rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle) : rotaryStartAngle;

        juce::Path fill;
        fill.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
        g.setColour(colours::accent);
        g.strokePath(fill, juce::PathStrokeType(lineWidth, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    // Pointer.
    juce::Path pointer;
    const auto pointerLength = arcRadius * 0.45f;
    pointer.addRoundedRectangle(-lineWidth * 0.35f, -arcRadius + 2.0f, lineWidth * 0.7f, pointerLength, 1.0f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centre));
    g.setColour(slider.isEnabled() ? colours::text : colours::textFaint);
    g.fillPath(pointer);
}

void Look::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

    auto fill = backgroundColour;
    if (shouldDrawButtonAsDown)
        fill = fill.brighter(0.25f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter(0.12f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, metrics::smallCornerRadius);
    g.setColour(colours::outline);
    g.drawRoundedRectangle(bounds, metrics::smallCornerRadius, 1.0f);
}

void Look::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted,
                            bool)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const auto boxSize = juce::jmin(15.0f, bounds.getHeight() - 2.0f);
    const juce::Rectangle<float> box{bounds.getX() + 1.0f, bounds.getCentreY() - boxSize * 0.5f, boxSize, boxSize};

    g.setColour(button.getToggleState() ? colours::accent : colours::panelRaised);
    g.fillRoundedRectangle(box, 3.0f);
    g.setColour(button.getToggleState() ? colours::accent : colours::outlineStrong);
    g.drawRoundedRectangle(box, 3.0f, 1.0f);

    if (button.getToggleState())
    {
        juce::Path tick;
        tick.startNewSubPath(box.getX() + boxSize * 0.24f, box.getCentreY());
        tick.lineTo(box.getCentreX() - boxSize * 0.02f, box.getBottom() - boxSize * 0.28f);
        tick.lineTo(box.getRight() - boxSize * 0.2f, box.getY() + boxSize * 0.26f);
        g.setColour(colours::background);
        g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour(button.isEnabled() ? (shouldDrawButtonAsHighlighted ? colours::text : colours::textDim)
                                   : colours::textFaint);
    g.setFont(juce::FontOptions(12.5f));
    g.drawText(button.getButtonText(), bounds.withTrimmedLeft(boxSize + 8.0f), juce::Justification::centredLeft,
               true);
}

void Look::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const juce::Rectangle<float> bounds{0.5f, 0.5f, static_cast<float>(width) - 1.0f,
                                        static_cast<float>(height) - 1.0f};

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, metrics::smallCornerRadius);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds, metrics::smallCornerRadius, 1.0f);

    juce::Path arrow;
    const auto cx = static_cast<float>(width) - 13.0f;
    const auto cy = static_cast<float>(height) * 0.5f;
    arrow.startNewSubPath(cx - 4.0f, cy - 2.0f);
    arrow.lineTo(cx, cy + 2.5f);
    arrow.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.strokePath(arrow, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font Look::getLabelFont(juce::Label& label)
{
    return label.getFont();
}

juce::Font Look::getComboBoxFont(juce::ComboBox&)
{
    return juce::Font(juce::FontOptions(13.0f));
}

juce::Font Look::getPopupMenuFont()
{
    return juce::Font(juce::FontOptions(13.5f));
}

void drawLevelMeter(juce::Graphics& g, juce::Rectangle<float> bounds, float level, bool vertical)
{
    g.setColour(colours::background);
    g.fillRoundedRectangle(bounds, 2.0f);

    const auto clamped = juce::jlimit(0.0f, 1.0f, level);
    if (clamped <= 0.001f)
        return;

    // Fixed thresholds rather than a gradient: it reads faster at a glance.
    const auto colour = clamped > 0.95f ? colours::meterClip
                        : clamped > 0.7f ? colours::meterHigh
                                         : colours::meterLow;

    auto filled = bounds;
    if (vertical)
        filled = bounds.withTop(bounds.getBottom() - bounds.getHeight() * clamped);
    else
        filled = bounds.withWidth(bounds.getWidth() * clamped);

    g.setColour(colour);
    g.fillRoundedRectangle(filled, 2.0f);
}

} // namespace blockrig::theme
