#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "dsp/AmpSlot.h"

namespace nammodeler
{

/// Serialisation of the per-slot model data.
///
/// The full .nam JSON is embedded (gzipped) rather than referenced by path, so
/// a session keeps working after the model file is moved, renamed, deleted, or
/// the project is opened on another machine. The original path is kept purely
/// for display and re-linking. .nam files are 50-300 KB and their weight arrays
/// compress well, so this costs very little state size.
namespace state
{
inline constexpr const char* kRootType = "NAMModelerState";
inline constexpr const char* kSlotType = "Slot";
inline constexpr const char* kSlotIndex = "index";
inline constexpr const char* kModelData = "modelData";
inline constexpr const char* kModelPath = "modelPath";
inline constexpr const char* kModelName = "modelName";

/// Writes a slot's model into a ValueTree child. An empty ModelInfo writes an
/// empty slot.
juce::ValueTree toValueTree(int slotIndex, const ModelInfo& info);

/// Reads a slot back. Returns false if the tree holds no model for this slot.
bool fromValueTree(const juce::ValueTree& slotTree, juce::String& jsonOut, juce::String& nameOut,
                   juce::String& pathOut);
} // namespace state

} // namespace nammodeler
