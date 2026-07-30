#include "ui/BlockCategories.h"

namespace blockrig
{
namespace
{
/// Keyword table. Ordered: the first category whose words appear wins, so the
/// specific ("cab", "reverb") is tested before the general ("amp", "fx").
struct CategoryRule
{
    BlockCategory category;
    std::initializer_list<const char*> words;
};

const std::initializer_list<CategoryRule> kRules = {
    {BlockCategory::cabinet, {"cabinet", "cab ", "speaker", " ir", "impulse"}},
    {BlockCategory::reverb, {"reverb", "verb", "hall", "plate", "room", "shimmer", "spring", "ambien"}},
    {BlockCategory::delay, {"delay", "echo", "slapback", "tape "}},
    {BlockCategory::modulation, {"chorus", "flange", "phaser", "phase ", "tremolo", "vibrato", "rotary",
                                 "modulat", "univibe", "ensemble"}},
    {BlockCategory::pitch, {"pitch", "harmon", "octave", "whammy", "detune"}},
    {BlockCategory::drive, {"distort", "overdrive", "drive", "fuzz", "saturat", "clip", "screamer", "boost",
                            "crunch"}},
    {BlockCategory::dynamics, {"compress", "limiter", "gate", "expander", "dynamic", "transient", "leveler",
                               "levelling"}},
    {BlockCategory::eq, {"equali", " eq", "eq ", "tone stack", "graphic", "parametric"}},
    {BlockCategory::filter, {"filter", "wah", "lowpass", "highpass", "formant", "envelope"}},
    {BlockCategory::amp, {"amp", "nam", "preamp", "head", "plexi", "marshall", "fender", "vox", "mesa",
                          "recto", "tube"}},
    {BlockCategory::utility, {"gain", "util", "meter", "analy", "split", "mix", "pan", "phase invert",
                              "trim"}},
};
} // namespace

juce::Array<BlockCategory> getAllCategories()
{
    // Signal-chain order, which is how players think about a rig.
    return {BlockCategory::drive,      BlockCategory::dynamics, BlockCategory::filter,
            BlockCategory::eq,         BlockCategory::amp,      BlockCategory::cabinet,
            BlockCategory::modulation, BlockCategory::pitch,    BlockCategory::delay,
            BlockCategory::reverb,     BlockCategory::utility,  BlockCategory::other};
}

juce::String getCategoryName(BlockCategory category)
{
    switch (category)
    {
        case BlockCategory::amp: return "Amp";
        case BlockCategory::cabinet: return "Cabinet";
        case BlockCategory::drive: return "Drive";
        case BlockCategory::eq: return "EQ";
        case BlockCategory::dynamics: return "Dynamics";
        case BlockCategory::delay: return "Delay";
        case BlockCategory::reverb: return "Reverb";
        case BlockCategory::modulation: return "Modulation";
        case BlockCategory::pitch: return "Pitch";
        case BlockCategory::filter: return "Filter";
        case BlockCategory::utility: return "Utility";
        case BlockCategory::other: break;
    }

    return "Other";
}

juce::Colour getCategoryColour(BlockCategory category)
{
    switch (category)
    {
        case BlockCategory::amp: return juce::Colour(0xffe8a33d);        // amber
        case BlockCategory::cabinet: return juce::Colour(0xff4fd1c5);    // teal
        case BlockCategory::drive: return juce::Colour(0xffe0574f);      // red
        case BlockCategory::eq: return juce::Colour(0xff5aa9f0);         // blue
        case BlockCategory::dynamics: return juce::Colour(0xff7c6cf0);   // indigo
        case BlockCategory::delay: return juce::Colour(0xff4fd1c5);      // teal
        case BlockCategory::reverb: return juce::Colour(0xff43c9a8);     // green-teal
        case BlockCategory::modulation: return juce::Colour(0xff5cc85c); // green
        case BlockCategory::pitch: return juce::Colour(0xffe07fc0);      // pink
        case BlockCategory::filter: return juce::Colour(0xffe8c53d);     // yellow
        case BlockCategory::utility: return juce::Colour(0xff9aa0aa);    // grey
        case BlockCategory::other: break;
    }

    return juce::Colour(0xff8b93a1);
}

BlockCategory categoriseBlock(const juce::PluginDescription& description)
{
    // The built-in amp is unambiguous; everything else is guesswork from text.
    if (description.pluginFormatName == "BlockRig" && description.fileOrIdentifier == "nam")
        return BlockCategory::amp;

    const auto haystack = (" " + description.name + " " + description.category + " "
                           + description.descriptiveName + " ")
                              .toLowerCase();

    for (const auto& rule : kRules)
        for (const auto* word : rule.words)
            if (haystack.contains(word))
                return rule.category;

    return BlockCategory::other;
}

void drawCategoryIcon(juce::Graphics& g, juce::Rectangle<float> bounds, BlockCategory category,
                      juce::Colour colour, float strokeWidth)
{
    // Everything is drawn in a unit square and then mapped onto `bounds`, so the
    // glyphs stay consistent at any size.
    juce::Path path;

    const auto moveTo = [&path](float x, float y) { path.startNewSubPath(x, y); };
    const auto lineTo = [&path](float x, float y) { path.lineTo(x, y); };

    switch (category)
    {
        case BlockCategory::amp:
            // An amp head: a box with a grille and a row of knobs.
            path.addRoundedRectangle(0.08f, 0.22f, 0.84f, 0.56f, 0.08f);
            moveTo(0.20f, 0.36f);
            lineTo(0.44f, 0.36f);
            moveTo(0.20f, 0.46f);
            lineTo(0.44f, 0.46f);
            moveTo(0.20f, 0.56f);
            lineTo(0.44f, 0.56f);
            path.addEllipse(0.58f, 0.40f, 0.10f, 0.10f);
            path.addEllipse(0.74f, 0.40f, 0.10f, 0.10f);
            break;

        case BlockCategory::cabinet:
            // An isometric box, as in the reference's cab icon.
            moveTo(0.16f, 0.30f);
            lineTo(0.58f, 0.14f);
            lineTo(0.86f, 0.32f);
            lineTo(0.86f, 0.70f);
            lineTo(0.44f, 0.86f);
            lineTo(0.16f, 0.68f);
            path.closeSubPath();
            moveTo(0.16f, 0.30f);
            lineTo(0.44f, 0.48f);
            lineTo(0.86f, 0.32f);
            moveTo(0.44f, 0.48f);
            lineTo(0.44f, 0.86f);
            break;

        case BlockCategory::drive:
            // A sine with its peaks squared off: clipping.
            moveTo(0.12f, 0.50f);
            path.quadraticTo(0.20f, 0.18f, 0.30f, 0.18f);
            lineTo(0.42f, 0.18f);
            path.quadraticTo(0.50f, 0.18f, 0.52f, 0.50f);
            path.quadraticTo(0.56f, 0.82f, 0.66f, 0.82f);
            lineTo(0.78f, 0.82f);
            path.quadraticTo(0.86f, 0.82f, 0.90f, 0.50f);
            break;

        case BlockCategory::eq:
            // Three faders, as in the reference's blue EQ icon.
            for (int i = 0; i < 3; ++i)
            {
                const auto x = 0.26f + static_cast<float>(i) * 0.24f;
                moveTo(x, 0.16f);
                lineTo(x, 0.84f);
                const auto handleY = 0.30f + static_cast<float>((i + 1) % 3) * 0.18f;
                path.addRoundedRectangle(x - 0.09f, handleY, 0.18f, 0.09f, 0.03f);
            }
            break;

        case BlockCategory::dynamics:
            // A target: level being pulled toward a centre.
            path.addEllipse(0.16f, 0.16f, 0.68f, 0.68f);
            path.addEllipse(0.36f, 0.36f, 0.28f, 0.28f);
            path.addEllipse(0.46f, 0.46f, 0.08f, 0.08f);
            break;

        case BlockCategory::delay:
            // Repeats, decaying left to right.
            moveTo(0.18f, 0.24f);
            lineTo(0.18f, 0.76f);
            moveTo(0.42f, 0.32f);
            lineTo(0.42f, 0.68f);
            moveTo(0.64f, 0.40f);
            lineTo(0.64f, 0.60f);
            moveTo(0.84f, 0.46f);
            lineTo(0.84f, 0.54f);
            break;

        case BlockCategory::reverb:
            // The spreading-wave shape the reference uses for reverb.
            moveTo(0.14f, 0.70f);
            lineTo(0.26f, 0.26f);
            lineTo(0.38f, 0.70f);
            lineTo(0.50f, 0.20f);
            lineTo(0.62f, 0.70f);
            lineTo(0.74f, 0.30f);
            lineTo(0.86f, 0.70f);
            break;

        case BlockCategory::modulation:
            // A sine with a triangle beneath it: an LFO.
            moveTo(0.12f, 0.40f);
            path.quadraticTo(0.30f, 0.06f, 0.50f, 0.40f);
            path.quadraticTo(0.70f, 0.74f, 0.88f, 0.40f);
            moveTo(0.34f, 0.82f);
            lineTo(0.50f, 0.62f);
            lineTo(0.66f, 0.82f);
            path.closeSubPath();
            break;

        case BlockCategory::pitch:
            // An interval: two notes, one stepped up.
            moveTo(0.22f, 0.74f);
            lineTo(0.22f, 0.36f);
            lineTo(0.50f, 0.28f);
            path.addEllipse(0.14f, 0.68f, 0.16f, 0.12f);
            moveTo(0.60f, 0.80f);
            lineTo(0.60f, 0.20f);
            moveTo(0.60f, 0.20f);
            lineTo(0.86f, 0.34f);
            break;

        case BlockCategory::filter:
            // A response curve rolling off.
            moveTo(0.12f, 0.68f);
            lineTo(0.44f, 0.68f);
            path.quadraticTo(0.62f, 0.68f, 0.68f, 0.34f);
            path.quadraticTo(0.74f, 0.68f, 0.88f, 0.68f);
            break;

        case BlockCategory::utility:
        case BlockCategory::other:
        default:
            // Neutral: a signal passing through.
            moveTo(0.14f, 0.50f);
            lineTo(0.86f, 0.50f);
            path.addEllipse(0.44f, 0.38f, 0.12f, 0.24f);
            break;
    }

    path.applyTransform(juce::AffineTransform::scale(bounds.getWidth(), bounds.getHeight())
                            .translated(bounds.getX(), bounds.getY()));

    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(strokeWidth, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

} // namespace blockrig
