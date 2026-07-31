#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace blockrig::theme
{

/// Every colour and metric lives here — no literals in components — so a design
/// pass is a change to one file rather than a hunt.
///
/// Palette per the approved design language (docs/18-UI-OVERHAUL.md): violet-
/// tinted blacks — never pure #000 — with one amber brand accent and a strict
/// category-colour system for blocks.
namespace colours
{
// Surfaces.
inline const juce::Colour background{0xff0b0a10}; ///< "ground" — app backdrop
inline const juce::Colour panel{0xff15131d};      ///< "raised" — panels, tiles, inputs
inline const juce::Colour panelRaised{0xff1d1a28}; ///< "raised-2" — buttons inside panels
inline const juce::Colour inset{0xff100e16};       ///< graph and waveform wells
inline const juce::Colour overlay{0xf212101a};     ///< floating menus and dialogs
inline const juce::Colour outline{0xff2c2839};     ///< standard border
inline const juce::Colour hairline{0xff221f2e};    ///< dividers, arc tracks
inline const juce::Colour outlineStrong{0xff3a3644};

// Text.
inline const juce::Colour text{0xfff4f5f7};      ///< strong
inline const juce::Colour textDim{0xffd6d3e0};   ///< body
inline const juce::Colour textFaint{0xff9b96ad}; ///< muted
inline const juce::Colour textGhost{0xff635f75}; ///< faint captions, footers
inline const juce::Colour disabled{0xff4a4558};

// Brand amber.
inline const juce::Colour accent{0xffe8a33d};
inline const juce::Colour accentBright{0xfff2c069};
inline const juce::Colour accentDeep{0xffc98a2e};
inline const juce::Colour accentDim = accentDeep;
inline const juce::Colour onAccent = background; ///< text on amber

// State.
inline const juce::Colour good{0xff5ed669};
inline const juce::Colour warn = accent;
inline const juce::Colour bad{0xffff6b70};
inline const juce::Colour mutedRed{0xffe5484d};

inline const juce::Colour meterLow = good;
inline const juce::Colour meterHigh = accentBright;
inline const juce::Colour meterClip = bad;
} // namespace colours

namespace metrics
{
// Radius scale: sm chips/buttons, md inputs/rows, lg cards/block chips,
// xl panels/dialogs.
inline constexpr float radiusSm = 8.0f;
inline constexpr float radiusMd = 11.0f;
inline constexpr float radiusLg = 16.0f;
inline constexpr float radiusXl = 18.0f;

inline constexpr float cornerRadius = radiusLg;
inline constexpr float smallCornerRadius = radiusSm;

inline constexpr int gap = 12;
inline constexpr int padding = 18;

inline constexpr int headerHeight = 60;
inline constexpr int footerHeight = 40;

/// Blocks are 64 px chips with the name beneath, per the chain spec.
inline constexpr int blockSquare = 64;
inline constexpr int blockLabelHeight = 30;
inline constexpr int blockWidth = blockSquare;
inline constexpr int blockHeight = blockSquare + blockLabelHeight;
inline constexpr int endBlockWidth = 64;
inline constexpr int arrowWidth = 30;
inline constexpr int laneHeight = blockHeight + 2 * gap;
} // namespace metrics

namespace fonts
{
/// The UI face (Space Grotesk). Weights: 400, 500, 700 — the design's 600 falls
/// back to 700, the closest static weight this family ships.
juce::Font ui(float size, int weight = 400);

/// The values face (IBM Plex Mono). For numerals only: BPM, dB, Hz, cents,
/// milliseconds, CPU %.
juce::Font mono(float size, int weight = 400);
} // namespace fonts

/// The mark: a 2×2 grid of rounded squares, three outlined, the bottom-right
/// solid amber. Reads from 12 px in a header to 72 px on the boot screen.
void drawLogoMark(juce::Graphics& g, juce::Rectangle<float> bounds);

/// "BLOCK" in text-strong + "RIG" in amber, Space Grotesk 700, tight tracking.
/// `height` sets the type size; the pair is centred in `bounds`.
void drawWordmark(juce::Graphics& g, juce::Rectangle<float> bounds, float height);

/// Colour for a block, keyed off the plug-in's category so a rig reads by hue.
juce::Colour colourForCategory(const juce::String& category, const juce::String& name);

/// Shared LookAndFeel.
///
/// Buttons: set the component property "primary" to true for the amber-gradient
/// treatment; everything else gets the raised-2 secondary style. Rotary sliders
/// take their category colour from rotarySliderFillColourId.
class Look final : public juce::LookAndFeel_V4
{
public:
    Look();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                      int buttonW, int buttonH, juce::ComboBox&) override;

    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator,
                           bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>& area,
                                    const juce::String& sectionName) override;
    int getPopupMenuBorderSize() override { return 8; }

    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
};

/// Range a meter displays. Guitar sits around -30 to -12 dBFS, which on a linear
/// scale is a barely visible sliver — so meters are drawn in dB.
inline constexpr float kMeterFloorDb = -60.0f;

/// Maps a linear peak (0..1) onto 0..1 of the meter's length, in dB.
float levelToMeterPosition(float linearLevel);

/// Draws a level meter as a bar. `level` is a linear peak; dB scaling happens
/// here.
void drawLevelMeter(juce::Graphics& g, juce::Rectangle<float> bounds, float level, bool vertical = false);

} // namespace blockrig::theme
