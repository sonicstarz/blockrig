#include "state/PluginState.h"

namespace nammodeler::state
{
namespace
{
juce::MemoryBlock compressString(const juce::String& text)
{
    juce::MemoryOutputStream compressed;
    {
        juce::GZIPCompressorOutputStream gzip(compressed);
        const auto utf8 = text.toRawUTF8();
        gzip.write(utf8, std::strlen(utf8));
    }
    return compressed.getMemoryBlock();
}

juce::String decompressBlock(const juce::MemoryBlock& block)
{
    juce::MemoryInputStream input(block, false);
    juce::GZIPDecompressorInputStream gzip(input);
    return gzip.readEntireStreamAsString();
}
} // namespace

juce::ValueTree toValueTree(int slotIndex, const ModelInfo& info)
{
    juce::ValueTree tree(kSlotType);
    tree.setProperty(kSlotIndex, slotIndex, nullptr);

    if (info.json.isNotEmpty())
    {
        tree.setProperty(kModelData, juce::var(compressString(info.json)), nullptr);
        tree.setProperty(kModelName, info.name, nullptr);
        tree.setProperty(kModelPath, info.path, nullptr);
    }

    return tree;
}

bool fromValueTree(const juce::ValueTree& slotTree, juce::String& jsonOut, juce::String& nameOut,
                   juce::String& pathOut)
{
    if (!slotTree.isValid() || !slotTree.hasProperty(kModelData))
        return false;

    const auto* block = slotTree.getProperty(kModelData).getBinaryData();
    if (block == nullptr || block->isEmpty())
        return false;

    jsonOut = decompressBlock(*block);
    if (jsonOut.isEmpty())
        return false;

    nameOut = slotTree.getProperty(kModelName).toString();
    pathOut = slotTree.getProperty(kModelPath).toString();
    return true;
}

} // namespace nammodeler::state
