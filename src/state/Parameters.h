#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace nammodeler
{

// Parameter IDs are frozen at first release: additive changes only, and the
// index order of choice parameters must never change (hosts store indices).
namespace pid
{
inline constexpr const char* inputMode = "input_mode";
inline constexpr const char* masterOut = "master_out";
inline constexpr const char* monoSum = "mono_sum";
inline constexpr const char* gateOn = "gate_on";
inline constexpr const char* gateThresh = "gate_thresh";

// Per-slot IDs are built by prefixing with "a_" / "b_".
inline constexpr const char* enabled = "enabled";
inline constexpr const char* inTrim = "in_trim";
inline constexpr const char* outTrim = "out_trim";
inline constexpr const char* pan = "pan";
inline constexpr const char* phase = "phase";
inline constexpr const char* solo = "solo";
inline constexpr const char* mute = "mute";
inline constexpr const char* eqOn = "eq_on";
inline constexpr const char* bass = "bass";
inline constexpr const char* mid = "mid";
inline constexpr const char* treble = "treble";
inline constexpr const char* outMode = "out_mode";
inline constexpr const char* calIn = "cal_in";
inline constexpr const char* calDbu = "cal_dbu";
inline constexpr const char* slim = "slim";

juce::String slotParam(int slotIndex, const char* baseId);
} // namespace pid

enum class InputMode
{
    mono = 0,
    stereo = 1
};

// Order must match the choice parameter and never change.
enum class OutputMode
{
    raw = 0,
    normalized = 1,
    calibrated = 2
};

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace nammodeler
