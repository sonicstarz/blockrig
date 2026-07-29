#pragma once

#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace blockrig
{

/// Serves BlockRig's own built-in blocks through the same AudioPluginFormat
/// interface as VST3 and AudioUnit.
///
/// The point is uniformity: the chain engine, the rig schema and the block
/// picker all deal in PluginDescription + AudioPluginInstance, so a built-in amp
/// needs no special case anywhere. (Same trick JUCE's own AudioPluginHost uses
/// for its internal processors.)
class InternalBlockFormat final : public juce::AudioPluginFormat
{
public:
    InternalBlockFormat();

    /// Every built-in block, for populating the picker.
    const std::vector<juce::PluginDescription>& getAllTypes() const { return mDescriptions; }

    static juce::String getIdentifier() { return "BlockRig"; }

    //==============================================================================
    juce::String getName() const override { return getIdentifier(); }
    bool fileMightContainThisPluginType(const juce::String&) override { return true; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }
    void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>&, const juce::String&) override;
    bool doesPluginStillExist(const juce::PluginDescription&) override { return true; }
    juce::String getNameOfPluginFromIdentifier(const juce::String& fileOrIdentifier) override
    {
        return fileOrIdentifier;
    }
    bool pluginNeedsRescanning(const juce::PluginDescription&) override { return false; }
    juce::StringArray searchPathsForPlugins(const juce::FileSearchPath&, bool, bool) override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation(const juce::PluginDescription&) const override
    {
        return false;
    }

private:
    void createPluginInstance(const juce::PluginDescription&, double initialSampleRate, int initialBufferSize,
                              PluginCreationCallback) override;

    static std::unique_ptr<juce::AudioPluginInstance> createInstance(const juce::String& identifier);

    std::vector<juce::PluginDescription> mDescriptions;
};

} // namespace blockrig
