#pragma once

namespace blockrig
{

/// Marker for built-in processors that preserve channel correlation: mono in,
/// mono-identical out.
///
/// The lane walk decides which blocks are "fed mono" by finding the first block
/// that could decorrelate the signal. Emitting two channels is not that - the
/// NAM block in true stereo runs the same model on both sides, so a mono feed
/// comes out as two identical channels and everything after it is still
/// effectively mono. A block wearing this marker passes mono-ness through
/// instead of ending it, so a ping-pong delay sitting behind two mono-fed amps
/// still negotiates mono-in and can actually ping-pong.
///
/// Only built-ins can wear it: for third-party plug-ins there is no way to know
/// whether they decorrelate, and assuming they do (the current behaviour) is
/// right for the reverbs and wideners people put in a chain on purpose.
class WidthNeutralProcessor
{
public:
    virtual ~WidthNeutralProcessor() = default;
};

} // namespace blockrig
