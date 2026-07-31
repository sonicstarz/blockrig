#include "ui/Theme.h"

#include "BinaryData.h"

namespace blockrig::theme
{
namespace fonts
{
namespace
{
juce::Typeface::Ptr load(const void* data, size_t size)
{
    return juce::Typeface::createSystemTypefaceFor(data, size);
}

struct Faces
{
    Faces()
        : uiRegular(load(BinaryData::SpaceGroteskRegular_ttf, BinaryData::SpaceGroteskRegular_ttfSize))
        , uiMedium(load(BinaryData::SpaceGroteskMedium_ttf, BinaryData::SpaceGroteskMedium_ttfSize))
        , uiBold(load(BinaryData::SpaceGroteskBold_ttf, BinaryData::SpaceGroteskBold_ttfSize))
        , monoRegular(load(BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize))
        , monoMedium(load(BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize))
        , monoSemiBold(load(BinaryData::IBMPlexMonoSemiBold_ttf, BinaryData::IBMPlexMonoSemiBold_ttfSize))
        , monoBold(load(BinaryData::IBMPlexMonoBold_ttf, BinaryData::IBMPlexMonoBold_ttfSize))
    {
    }

    juce::Typeface::Ptr uiRegular, uiMedium, uiBold;
    juce::Typeface::Ptr monoRegular, monoMedium, monoSemiBold, monoBold;
};

const Faces& faces()
{
    static Faces instance;
    return instance;
}
} // namespace

juce::Font ui(float size, int weight)
{
    const auto& f = faces();
    const auto face = weight >= 650 ? f.uiBold : weight >= 500 ? f.uiMedium : f.uiRegular;
    return juce::Font(juce::FontOptions(face).withHeight(size));
}

juce::Font mono(float size, int weight)
{
    const auto& f = faces();
    const auto face = weight >= 700   ? f.monoBold
                      : weight >= 600 ? f.monoSemiBold
                      : weight >= 500 ? f.monoMedium
                                      : f.monoRegular;
    return juce::Font(juce::FontOptions(face).withHeight(size));
}
} // namespace fonts

//==============================================================================
void drawLogoMark(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const auto size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto area = bounds.withSizeKeepingCentre(size, size);

    const auto gapPx = juce::jmax(1.5f, size * 0.12f);
    const auto square = (size - gapPx) * 0.5f;
    const auto radius = juce::jmax(1.5f, square * 0.28f);
    const auto stroke = juce::jmax(1.0f, square / 9.0f);

    const auto at = [&](int column, int row) {
        return juce::Rectangle<float>(area.getX() + column * (square + gapPx),
                                      area.getY() + row * (square + gapPx), square, square);
    };

    g.setColour(colours::outlineStrong);
    for (const auto& cell : {at(0, 0), at(1, 0), at(0, 1)})
        g.drawRoundedRectangle(cell.reduced(stroke * 0.5f), radius, stroke);

    // The amber square gets a whisper of glow even at header size.
    const auto amber = at(1, 1);
    g.setColour(colours::accent.withAlpha(0.28f));
    g.fillRoundedRectangle(amber.expanded(size * 0.06f), radius + 1.0f);
    g.setColour(colours::accent);
    g.fillRoundedRectangle(amber, radius);
}

void drawWordmark(juce::Graphics& g, juce::Rectangle<float> bounds, float height)
{
    auto font = fonts::ui(height, 700);
    font = font.withExtraKerningFactor(-0.02f);

    const juce::String block("BLOCK"), rig("RIG");
    const auto blockWidth = juce::GlyphArrangement::getStringWidth(font, block);
    const auto rigWidth = juce::GlyphArrangement::getStringWidth(font, rig);
    const auto x = bounds.getCentreX() - (blockWidth + rigWidth) * 0.5f;
    const auto baseline = bounds.getCentreY() + font.getAscent() * 0.5f - font.getDescent() * 0.25f;

    g.setFont(font);
    g.setColour(colours::text);
    g.drawSingleLineText(block, juce::roundToInt(x), juce::roundToInt(baseline));
    g.setColour(colours::accent);
    g.drawSingleLineText(rig, juce::roundToInt(x + blockWidth), juce::roundToInt(baseline));
}

//==============================================================================
Look::Look()
{
    setDefaultSansSerifTypeface(fonts::ui(14.0f).getTypefacePtr());

    setColour(juce::ResizableWindow::backgroundColourId, colours::background);
    setColour(juce::Label::textColourId, colours::text);

    setColour(juce::Slider::rotarySliderFillColourId, colours::accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, colours::hairline);
    setColour(juce::Slider::textBoxTextColourId, colours::text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::trackColourId, colours::accent);
    setColour(juce::Slider::backgroundColourId, colours::hairline);

    setColour(juce::TextButton::buttonColourId, colours::panelRaised);
    setColour(juce::TextButton::buttonOnColourId, colours::accentDeep);
    setColour(juce::TextButton::textColourOffId, colours::textDim);
    setColour(juce::TextButton::textColourOnId, colours::text);

    setColour(juce::ComboBox::backgroundColourId, colours::panelRaised);
    setColour(juce::ComboBox::outlineColourId, colours::outline);
    setColour(juce::ComboBox::textColourId, colours::textDim);
    setColour(juce::ComboBox::arrowColourId, colours::textFaint);

    setColour(juce::PopupMenu::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::PopupMenu::textColourId, colours::textDim);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colours::panelRaised);
    setColour(juce::PopupMenu::highlightedTextColourId, colours::text);

    setColour(juce::TextEditor::backgroundColourId, colours::background);
    setColour(juce::TextEditor::outlineColourId, colours::outline);
    setColour(juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour(juce::TextEditor::textColourId, colours::text);
    setColour(juce::TextEditor::highlightColourId, colours::accentDeep.withAlpha(0.6f));

    setColour(juce::CaretComponent::caretColourId, colours::accent);
    setColour(juce::ScrollBar::thumbColourId, colours::outlineStrong);
    setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);

    setColour(juce::TooltipWindow::backgroundColourId, colours::overlay);
    setColour(juce::TooltipWindow::textColourId, colours::textDim);
    setColour(juce::TooltipWindow::outlineColourId, colours::outline);

    setColour(juce::AlertWindow::backgroundColourId, colours::panel);
    setColour(juce::AlertWindow::textColourId, colours::textDim);
    setColour(juce::AlertWindow::outlineColourId, colours::outline);
}

//==============================================================================
// The "soft-glass" knob: 270° hairline track, category-coloured value arc with
// a glow, a radially lit centre disc, and a rotating needle in a light tint.
void Look::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                            float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const auto category = slider.findColour(juce::Slider::rotarySliderFillColourId);
    const auto lineWidth = juce::jlimit(4.0f, 7.0f, radius * 0.18f);
    const auto arcRadius = radius - lineWidth * 0.5f;

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle,
                        true);
    g.setColour(colours::hairline);
    g.strokePath(track, juce::PathStrokeType(lineWidth, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    if (slider.isEnabled())
    {
        // Bipolar controls fill from the centre outwards.
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const auto startAngle =
            bipolar ? rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle) : rotaryStartAngle;

        juce::Path fill;
        fill.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);

        // Glow first — the same path, wider and translucent — then the arc.
        g.setColour(category.withAlpha(0.35f));
        g.strokePath(fill, juce::PathStrokeType(lineWidth + 4.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
        g.setColour(category);
        g.strokePath(fill, juce::PathStrokeType(lineWidth, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }

    // Centre disc, lit from the upper left.
    const auto discRadius = arcRadius - lineWidth * 0.5f - 4.0f;

    if (discRadius > 4.0f)
    {
        const juce::Rectangle<float> disc(centre.x - discRadius, centre.y - discRadius, discRadius * 2.0f,
                                          discRadius * 2.0f);

        juce::ColourGradient lit(category.withAlpha(slider.isEnabled() ? 0.16f : 0.06f),
                                 disc.getX() + disc.getWidth() * 0.35f,
                                 disc.getY() + disc.getHeight() * 0.28f,
                                 colours::panel.withAlpha(0.9f), centre.x, disc.getBottom(), true);
        g.setGradientFill(lit);
        g.fillEllipse(disc);
        g.setColour(category.withAlpha(slider.isEnabled() ? 0.35f : 0.15f));
        g.drawEllipse(disc, 1.0f);

        // Needle: a light tint of the category colour, with its own glow.
        const auto needle = category.interpolatedWith(juce::Colours::white, 0.55f);
        const auto needleLength = juce::jmax(8.0f, discRadius * 0.5f);

        juce::Path pointer;
        pointer.addRoundedRectangle(-2.0f, -discRadius + 3.0f, 4.0f, needleLength, 2.0f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centre));

        g.setColour(needle.withAlpha(0.4f));
        g.strokePath(pointer, juce::PathStrokeType(3.0f));
        g.setColour(slider.isEnabled() ? needle : colours::disabled);
        g.fillPath(pointer);
    }
}

juce::Label* Look::createSliderTextBox(juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(fonts::mono(12.0f));
    label->setJustificationType(juce::Justification::centred);
    return label;
}

//==============================================================================
void Look::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const bool primary = button.getProperties()["primary"];

    if (primary && button.isEnabled())
    {
        // Amber gradient with a soft glow: the one emphasised action on a screen.
        g.setColour(colours::accent.withAlpha(shouldDrawButtonAsDown ? 0.2f : 0.35f));
        g.fillRoundedRectangle(bounds.expanded(2.5f), metrics::radiusMd + 2.0f);

        juce::ColourGradient gradient(shouldDrawButtonAsDown ? colours::accent : colours::accentBright,
                                      0.0f, bounds.getY(),
                                      shouldDrawButtonAsDown ? colours::accentDeep : colours::accent, 0.0f,
                                      bounds.getBottom(), false);
        g.setGradientFill(gradient);
        g.fillRoundedRectangle(bounds, metrics::radiusMd);
        return;
    }

    auto fill = backgroundColour == findColour(juce::TextButton::buttonColourId)
                    ? colours::panelRaised
                    : backgroundColour;

    if (shouldDrawButtonAsDown)
        fill = fill.brighter(0.22f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter(0.1f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, metrics::radiusMd);
    g.setColour(colours::outline);
    g.drawRoundedRectangle(bounds, metrics::radiusMd, 1.0f);

    // Keyboard focus: the amber ring the design asks for on every control.
    if (button.hasKeyboardFocus(true))
    {
        g.setColour(colours::accent.withAlpha(0.9f));
        g.drawRoundedRectangle(bounds, metrics::radiusMd, 2.0f);
    }
}

void Look::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const bool primary = button.getProperties()["primary"];
    const bool on = button.getToggleState();

    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.setColour(!button.isEnabled() ? colours::disabled
                : primary           ? colours::onAccent
                : on                ? colours::accentBright
                                    : colours::textDim);
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(4, 2),
                     juce::Justification::centred, 1, 0.85f);
}

juce::Font Look::getTextButtonFont(juce::TextButton& button, int buttonHeight)
{
    const bool primary = button.getProperties()["primary"];
    return fonts::ui(juce::jmin(14.0f, static_cast<float>(buttonHeight) * 0.55f), primary ? 700 : 500);
}

void Look::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted,
                            bool)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const auto boxSize = juce::jmin(15.0f, bounds.getHeight() - 2.0f);
    const juce::Rectangle<float> box{bounds.getX() + 1.0f, bounds.getCentreY() - boxSize * 0.5f, boxSize,
                                     boxSize};

    // Checked: solid amber square with a dark tick. Unchecked: outline only.
    if (button.getToggleState())
    {
        g.setColour(colours::accent);
        g.fillRoundedRectangle(box, 4.0f);

        juce::Path tick;
        tick.startNewSubPath(box.getX() + boxSize * 0.24f, box.getCentreY());
        tick.lineTo(box.getCentreX() - boxSize * 0.02f, box.getBottom() - boxSize * 0.28f);
        tick.lineTo(box.getRight() - boxSize * 0.2f, box.getY() + boxSize * 0.26f);
        g.setColour(colours::onAccent);
        g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }
    else
    {
        g.setColour(colours::outlineStrong);
        g.drawRoundedRectangle(box.reduced(0.75f), 4.0f, 1.5f);
    }

    g.setColour(!button.isEnabled()             ? colours::disabled
                : shouldDrawButtonAsHighlighted ? colours::text
                                                : colours::textDim);
    g.setFont(fonts::ui(13.0f));
    g.drawText(button.getButtonText(), bounds.withTrimmedLeft(boxSize + 9.0f),
               juce::Justification::centredLeft, true);
}

void Look::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                        juce::ComboBox& box)
{
    const juce::Rectangle<float> bounds{0.5f, 0.5f, static_cast<float>(width) - 1.0f,
                                        static_cast<float>(height) - 1.0f};

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, metrics::radiusMd);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds, metrics::radiusMd, 1.0f);

    juce::Path arrow;
    const auto cx = static_cast<float>(width) - 13.0f;
    const auto cy = static_cast<float>(height) * 0.5f;
    arrow.startNewSubPath(cx - 4.0f, cy - 2.0f);
    arrow.lineTo(cx, cy + 2.5f);
    arrow.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.strokePath(arrow, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

//==============================================================================
void Look::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    const juce::Rectangle<float> bounds(0.0f, 0.0f, static_cast<float>(width),
                                        static_cast<float>(height));

    g.setColour(colours::overlay);
    g.fillRoundedRectangle(bounds.reduced(0.5f), metrics::radiusLg);
    g.setColour(colours::outline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), metrics::radiusLg, 1.0f);
}

void Look::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                             bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                             const juce::String& text, const juce::String& shortcutKeyText,
                             const juce::Drawable*, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour(colours::hairline);
        g.fillRect(area.reduced(8, 0).withHeight(1).withY(area.getCentreY()));
        return;
    }

    auto row = area.toFloat().reduced(2.0f, 1.0f);

    if (isHighlighted && isActive)
    {
        g.setColour(colours::panelRaised);
        g.fillRoundedRectangle(row, 10.0f);
    }

    auto textArea = row.reduced(10.0f, 0.0f);

    if (isTicked)
    {
        // A small amber dot rather than a checkmark: quieter, still unambiguous.
        g.setColour(colours::accent);
        g.fillEllipse(textArea.getX() - 2.0f, row.getCentreY() - 2.5f, 5.0f, 5.0f);
    }

    textArea.removeFromLeft(10.0f);

    g.setColour(!isActive                    ? colours::disabled
                : textColour != nullptr      ? *textColour
                : isHighlighted              ? colours::text
                                             : colours::textDim);
    g.setFont(fonts::ui(14.0f));
    g.drawFittedText(text, textArea.toNearestInt(), juce::Justification::centredLeft, 1);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour(colours::textGhost);
        g.setFont(fonts::mono(11.0f));
        g.drawText(shortcutKeyText, textArea.toNearestInt(), juce::Justification::centredRight, true);
    }

    if (hasSubMenu)
    {
        juce::Path chevron;
        const auto cx = row.getRight() - 12.0f;
        chevron.startNewSubPath(cx - 2.5f, row.getCentreY() - 4.0f);
        chevron.lineTo(cx + 2.5f, row.getCentreY());
        chevron.lineTo(cx - 2.5f, row.getCentreY() + 4.0f);
        g.setColour(colours::textFaint);
        g.strokePath(chevron, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }
}

void Look::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area,
                                      const juce::String& sectionName)
{
    g.setColour(colours::textFaint);
    g.setFont(fonts::ui(11.0f, 500));
    g.drawText(sectionName, area.reduced(12, 0), juce::Justification::centredLeft, true);
}

//==============================================================================
juce::Font Look::getLabelFont(juce::Label& label)
{
    return label.getFont();
}

juce::Font Look::getComboBoxFont(juce::ComboBox&)
{
    return fonts::ui(13.0f);
}

juce::Font Look::getPopupMenuFont()
{
    return fonts::ui(14.0f);
}

juce::Font Look::getAlertWindowTitleFont()
{
    return fonts::ui(17.0f, 700);
}

juce::Font Look::getAlertWindowMessageFont()
{
    return fonts::ui(14.0f);
}

juce::Font Look::getAlertWindowFont()
{
    return fonts::ui(13.0f);
}

//==============================================================================
juce::Colour colourForCategory(const juce::String& category, const juce::String& name)
{
    const auto haystack = (category + " " + name).toLowerCase();

    const auto has = [&haystack](std::initializer_list<const char*> words) {
        for (const auto* word : words)
            if (haystack.contains(word))
                return true;
        return false;
    };

    // The strict category system: these hexes are the design's, verbatim.
    if (has({"dist", "drive", "fuzz", "od", "sat", "clip"}))
        return juce::Colour(0xffe5484d);
    if (has({"amp", "nam", "preamp"}))
        return juce::Colour(0xffe8a33d);
    if (has({"cab", "speaker", "impulse", "ir "}))
        return juce::Colour(0xff2ebfa5);
    if (has({"eq", "filter", "tone"}))
        return juce::Colour(0xff4c8dff);
    if (has({"chorus", "flange", "phase", "mod", "trem", "vibr"}))
        return juce::Colour(0xff5bc24c);
    if (has({"delay", "echo", "tape"}))
        return juce::Colour(0xff35b6e0);
    if (has({"reverb", "verb", "room", "hall", "space"}))
        return juce::Colour(0xff9b6df2);
    if (has({"pitch", "harm", "octave"}))
        return juce::Colour(0xffe5559c);

    return juce::Colour(0xff7b8494); // utility grey
}

float levelToMeterPosition(float linearLevel)
{
    if (linearLevel <= 0.0f)
        return 0.0f;

    const auto db = juce::Decibels::gainToDecibels(linearLevel, kMeterFloorDb);
    return juce::jlimit(0.0f, 1.0f, (db - kMeterFloorDb) / -kMeterFloorDb);
}

void drawLevelMeter(juce::Graphics& g, juce::Rectangle<float> bounds, float level, bool vertical)
{
    g.setColour(colours::inset);
    g.fillRoundedRectangle(bounds, 2.0f);

    g.setColour(colours::hairline);
    for (const float markDb : {-12.0f, -3.0f})
    {
        const auto position = (markDb - kMeterFloorDb) / -kMeterFloorDb;

        if (vertical)
        {
            const auto y = bounds.getBottom() - bounds.getHeight() * position;
            g.fillRect(bounds.getX(), y, bounds.getWidth(), 1.0f);
        }
        else
        {
            const auto x = bounds.getX() + bounds.getWidth() * position;
            g.fillRect(x, bounds.getY(), 1.0f, bounds.getHeight());
        }
    }

    const auto position = levelToMeterPosition(level);

    if (position <= 0.001f)
        return;

    const auto db = juce::Decibels::gainToDecibels(level, kMeterFloorDb);
    const auto colour = db > -0.5f ? colours::meterClip : db > -6.0f ? colours::meterHigh : colours::meterLow;

    auto filled = bounds;
    if (vertical)
        filled = bounds.withTop(bounds.getBottom() - bounds.getHeight() * position);
    else
        filled = bounds.withWidth(bounds.getWidth() * position);

    g.setColour(colour);
    g.fillRoundedRectangle(filled, 2.0f);
}

} // namespace blockrig::theme
