#pragma once

/// AudioDSPTools' ResamplingContainer was lifted from iPlug2 and still expects
/// two symbols from that framework: `iplug::PI` and `DEFAULT_BLOCK_SIZE`. We
/// build against JUCE, so we supply them here and include the container through
/// this header rather than directly.
///
/// Values match iPlug2's own definitions, so the resampler behaves identically
/// to the one in the official NAM plugin.

namespace iplug
{
static constexpr double PI = 3.14159265358979323846;
} // namespace iplug

#ifndef DEFAULT_BLOCK_SIZE
    #define DEFAULT_BLOCK_SIZE 1024
#endif

#include <dsp/ResamplingContainer/ResamplingContainer.h>
