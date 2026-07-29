#include "host/InternalBlockFormat.h"

#include "blocks/nam/NamBlockProcessor.h"

namespace blockrig
{

InternalBlockFormat::InternalBlockFormat()
{
    mDescriptions.push_back(NamBlockProcessor::getBlockDescription());
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
