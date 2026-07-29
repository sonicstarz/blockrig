#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace blockrig
{
class BlockRigProcessor;

/// Serialisation of a whole rig, implementing docs/14-SCHEMA.md.
///
/// One document serves three roles: the standalone rig file, the DAW state
/// chunk, and (later) preset-pool entries — so there is one code path and one
/// set of tests for all of them.
///
/// Child plug-in state is stored opaquely, exactly as the hosted instance
/// produced it. It must never be synthesised or edited: a raw processor chunk is
/// not valid VST3 component state, and a mismatched chunk is ignored *silently*
/// (measured in P0). Restoring therefore verifies rather than assumes.
namespace rigstate
{
inline constexpr int kSchemaVersion = 1;

namespace ids
{
inline constexpr const char* root = "BlockRig";
inline constexpr const char* schemaVersion = "schemaVersion";
inline constexpr const char* name = "name";
inline constexpr const char* uuid = "uuid";
inline constexpr const char* createdUtc = "createdUtc";
inline constexpr const char* modifiedUtc = "modifiedUtc";
inline constexpr const char* appVersion = "appVersion";

inline constexpr const char* input = "Input";
inline constexpr const char* output = "Output";
inline constexpr const char* inputModeAttr = "mode";
inline constexpr const char* gainDb = "gainDb";
inline constexpr const char* monoSum = "monoSum";

inline constexpr const char* lane = "Lane";
inline constexpr const char* stage = "Stage";
inline constexpr const char* row = "Row";
inline constexpr const char* block = "Block";
inline constexpr const char* blockUid = "uid";
inline constexpr const char* format = "format";
inline constexpr const char* identifier = "identifier";
inline constexpr const char* uniqueId = "uniqueId";
inline constexpr const char* manufacturer = "manufacturer";
inline constexpr const char* version = "version";
inline constexpr const char* bypassed = "bypassed";
inline constexpr const char* state = "State";
inline constexpr const char* encoding = "encoding";
} // namespace ids

/// Captures the processor's current rig.
juce::ValueTree toValueTree(BlockRigProcessor& processor);

/// Result of a restore, so the UI can tell the user what did not come back.
struct RestoreResult
{
    int blocksRequested = 0;
    int blocksCreated = 0;
    juce::StringArray missingPlugins;   ///< could not be instantiated at all
    juce::StringArray stateNotRestored; ///< created, but refused their saved state
    juce::String error;                 ///< set when the document itself is unusable
};

/// Rebuilds the rig. Blocks are created one at a time in lane order (plug-in
/// creation is asynchronous, so this is what keeps the order deterministic);
/// `onFinished` is called on the message thread when the whole rig is up.
void restore(BlockRigProcessor& processor, const juce::ValueTree& rig,
             std::function<void(RestoreResult)> onFinished = {});

} // namespace rigstate
} // namespace blockrig
