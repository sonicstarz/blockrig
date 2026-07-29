#include "BlockRigProcessor.h"

/// Entry point for the VST3/AU build. The editor itself lives with the processor
/// so the app and the plug-in share one implementation.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new blockrig::BlockRigProcessor();
}
