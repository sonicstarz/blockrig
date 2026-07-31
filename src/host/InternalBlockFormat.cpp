#include "host/InternalBlockFormat.h"

#include "blocks/eq/EqBlockProcessor.h"
#include "blocks/ir/IrBlockProcessor.h"
#include "blocks/nam/NamBlockProcessor.h"
#include "blocks/utility/UtilityBlockProcessor.h"

namespace blockrig
{

InternalBlockFormat::InternalBlockFormat()
{
    // Signal-chain order, which is the order the picker shows them in.
    mDescriptions.push_back(NamBlockProcessor::getBlockDescription());
    mDescriptions.push_back(IrBlockProcessor::getBlockDescription());
    mDescriptions.push_back(EqBlockProcessor::getBlockDescription());
    mDescriptions.push_back(UtilityBlockProcessor::getBlockDescription());
}

void InternalBlockFormat::findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& results,
                                              const juce::String& fileOrIdentifier)
{
    for (const auto& description : mDescriptions)
        if (description.fileOrIdentifier == fileOrIdentifier)
            results.add(new juce::PluginDescription(description));
}

std::unique_ptr<juce::AudioPluginInstance> InternalBlockFormat::createInstance(const juce::String& identifier)
{
    if (identifier == NamBlockProcessor::kIdentifier)
        return std::make_unique<NamBlockProcessor>();
    if (identifier == IrBlockProcessor::kIdentifier)
        return std::make_unique<IrBlockProcessor>();
    if (identifier == EqBlockProcessor::kIdentifier)
        return std::make_unique<EqBlockProcessor>();
    if (identifier == UtilityBlockProcessor::kIdentifier)
        return std::make_unique<UtilityBlockProcessor>();

    return nullptr;
}

void InternalBlockFormat::createPluginInstance(const juce::PluginDescription& description, double, int,
                                               PluginCreationCallback callback)
{
    if (auto instance = createInstance(description.fileOrIdentifier))
        callback(std::move(instance), {});
    else
        callback(nullptr, "Unknown built-in block: " + description.fileOrIdentifier);
}

} // namespace blockrig
