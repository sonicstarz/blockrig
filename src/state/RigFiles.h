#pragma once

#include <functional>

#include <juce_data_structures/juce_data_structures.h>

#include "state/RigState.h"

namespace blockrig
{
class BlockRigProcessor;

/// Reading and writing `.blockrig` files.
///
/// Deliberately the same document the DAW chunk uses, so a rig saved from the
/// app and a rig embedded in a session are the same bytes and exercise the same
/// code — one format, one set of bugs.
namespace rigfiles
{
inline constexpr const char* kFileExtension = ".blockrig";
inline constexpr const char* kFileWildcard = "*.blockrig";

/// Where rigs live by default. Under Documents rather than Application Support
/// because these are the user's work, not the app's bookkeeping.
juce::File getDefaultDirectory();

bool save(BlockRigProcessor& processor, const juce::File& file, juce::String& errorOut);

/// Asynchronous, because restoring instantiates plug-ins one at a time.
void load(BlockRigProcessor& processor, const juce::File& file,
          std::function<void(rigstate::RestoreResult, juce::String error)> onFinished);

} // namespace rigfiles
} // namespace blockrig
