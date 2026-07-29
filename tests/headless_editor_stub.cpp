// Headless targets link the processor but not the interface, so they supply this
// stub instead of ui/HostedEditor.cpp. Nothing in a headless test opens an editor.

#include "BlockRigProcessor.h"

namespace blockrig
{
juce::AudioProcessorEditor* BlockRigProcessor::createEditor()
{
    return nullptr;
}
} // namespace blockrig
