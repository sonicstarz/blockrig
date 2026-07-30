#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace blockrig::theme
{

/// Every colour and metric lives here — no literals in components — so the
/// visual pass in P5 is a change to one file rather than a hunt.
///
/// Direction: flat, dark, high contrast, one warm accent. The reference points
/// are GENOME and Quad Cortex rather than skeuomorphic amp panels.
namespace colours
{
inline const juce::Colour background{0xff101216};
inline const juce::Colour panel{0xff1a1d23};
inline const juce::Colour panelRaised{0xff23272f};
inline const juce::Colour outline{0xff31363f};
inline const juce::Colour outlineStrong{0xff454b56};

inline const juce::Colour text{0xffe8eaed};
inline const juce::Colour textDim{0xff9aa0aa};
inline const juce::Colour textFaint{0xff6b7280};

/// Warm amber, deliberately away from the teal/blue most amp sims use.
inline const juce::Colour accent{0xffe8a33d};
inline const juce::Colour accentDim{0xff8a6224};

inline const juce::Colour good{0xff5cb85c};
inline const juce::Colour warn{0xffe8a33d};
inline const juce::Colour bad{0xffe05252};

inline const juce::Colour meterLow{0xff4a9e5c};
inline const juce::Colour meterHigh{0xffe8a33d};
inline const juce::Colour meterClip{0xffe05252};
} // namespace colours

namespace metrics
{
inline constexpr float cornerRadius = 7.0f;
inline constexpr float smallCornerRadius = 4.0f;
inline constexpr int gap = 10;
inline constexpr int padding = 14;

inline constexpr int headerHeight = 52;
inline constexpr int footerHeight = 38;

/// Blocks are compact squares with the name beneath, rather than wide cards.
/// A rig is read by shape and colour at a glance; long titles inside every tile
/// turn the chain into a wall of text.
inline constexpr int blockSquare = 58;
inline constexpr int blockLabelHeight = 26;
inline constexpr int blockWidth = blockSquare;
inline constexpr int blockHeight = blockSquare + blockLabelHeight;
inline constexpr int endBlockWidth = 52;
inline constexpr int arrowWidth = 26;
inline constexpr int laneHeight = blockHeight + 2 * gap;
} // namespace metrics

/// Colour for a block, keyed off the plug-in's category so a rig reads by hue:
/// amps warm, time-based cool, dynamics blue, and so on.
juce::Colour colourForCategory(const juce::String& category, const juce::String& name);

/// Shared LookAndFeel. Kept small on purpose: components draw themselves from
/// the palette above, and this only covers the stock widgets we reuse.
class Look final : public juce::LookAndFeel_V4
{
public:
    Look();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                      int buttonW, int buttonH, juce::ComboBox&) override;

    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

/// Range a meter displays. Guitar sits around -30 to -12 dBFS, which on a linear
/// scale is a barely visible sliver — so meters are drawn in dB.
inline constexpr float kMeterFloorDb = -60.0f;

/// Maps a linear peak (0..1) onto 0..1 of the meter's length, in dB.
float levelToMeterPosition(float linearLevel);

/// Draws a level meter as a bar. Shared by the I/O blocks and the per-block
/// strips. `level` is a linear peak; the scaling to dB happens here.
void drawLevelMeter(juce::Graphics& g, juce::Rectangle<float> bounds, float level, bool vertical = false);

} // namespace blockrig::theme
