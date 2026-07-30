#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace blockrig
{

/// What kind of thing a block is.
///
/// Plug-ins describe themselves inconsistently — categories range from "Fx" to
/// nothing at all — so this is derived from the description *and* the name, then
/// used everywhere: the colour of a block, its icon, and how the picker groups.
/// A rig should be readable by shape and hue before any text is read.
enum class BlockCategory
{
    amp,
    cabinet,
    drive,
    eq,
    dynamics,
    delay,
    reverb,
    modulation,
    pitch,
    filter,
    utility,
    other
};

/// All categories in the order the picker should present them: the order a
/// guitarist builds a chain in, not alphabetical.
juce::Array<BlockCategory> getAllCategories();

juce::String getCategoryName(BlockCategory category);
juce::Colour getCategoryColour(BlockCategory category);

/// Works out a category from what the plug-in says about itself plus its name.
BlockCategory categoriseBlock(const juce::PluginDescription& description);

/// Draws the category's line-art glyph inside `bounds`.
void drawCategoryIcon(juce::Graphics& g, juce::Rectangle<float> bounds, BlockCategory category,
                      juce::Colour colour, float strokeWidth = 1.6f);

} // namespace blockrig
