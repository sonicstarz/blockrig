#include "state/BlockFavorites.h"

#include "host/BlockInstance.h"

namespace blockrig
{
namespace
{
constexpr const char* kExtension = ".blockfav";

juce::String toBase64(const juce::MemoryBlock& block)
{
    return juce::Base64::toBase64(block.getData(), block.getSize());
}

juce::MemoryBlock fromBase64(const juce::String& text)
{
    juce::MemoryOutputStream stream;
    juce::Base64::convertFromBase64(stream, text);
    return stream.getMemoryBlock();
}
} // namespace

BlockFavorites::BlockFavorites()
    : mDirectory(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                     .getChildFile("Application Support")
                     .getChildFile("BlockRig")
                     .getChildFile("Favorites"))
{
    mDirectory.createDirectory();
}

juce::Array<BlockFavorites::Entry> BlockFavorites::getEntries() const
{
    juce::Array<Entry> entries;

    for (const auto& file : mDirectory.findChildFiles(juce::File::findFiles, true,
                                                      juce::String("*") + kExtension))
    {
        const auto tree = juce::ValueTree::fromXml(file.loadFileAsString());

        if (!tree.isValid())
            continue;

        Entry entry;
        entry.file = file;
        entry.name = tree.getProperty("name", file.getFileNameWithoutExtension()).toString();
        entry.description.name = tree.getProperty("pluginName", "").toString();
        entry.description.pluginFormatName = tree.getProperty("format", "").toString();
        entry.description.fileOrIdentifier = tree.getProperty("identifier", "").toString();
        entry.description.manufacturerName = tree.getProperty("manufacturer", "").toString();
        entry.description.uniqueId = tree.getProperty("uniqueId", 0);

        entries.add(entry);
    }

    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.name.compareNatural(b.name) < 0; });

    return entries;
}

bool BlockFavorites::save(const BlockInstance& block, const juce::String& name)
{
    auto* plugin = block.getPlugin();

    if (plugin == nullptr)
        return false;

    juce::PluginDescription description;
    plugin->fillInPluginDescription(description);

    juce::MemoryBlock chunk;
    plugin->getStateInformation(chunk);

    juce::ValueTree tree("BlockFavorite");
    tree.setProperty("name", name, nullptr);
    tree.setProperty("pluginName", description.name, nullptr);
    tree.setProperty("format", description.pluginFormatName, nullptr);
    tree.setProperty("identifier", description.fileOrIdentifier, nullptr);
    tree.setProperty("manufacturer", description.manufacturerName, nullptr);
    tree.setProperty("uniqueId", description.uniqueId, nullptr);
    tree.setProperty("state", toBase64(chunk), nullptr);

    const auto file = mDirectory
                          .getChildFile(juce::File::createLegalFileName(name) + kExtension)
                          .getNonexistentSibling();

    if (!file.replaceWithText(tree.toXmlString()))
        return false;

    if (onChanged)
        onChanged();

    return true;
}

juce::MemoryBlock BlockFavorites::loadState(const juce::File& file) const
{
    const auto tree = juce::ValueTree::fromXml(file.loadFileAsString());

    if (!tree.isValid())
        return {};

    return fromBase64(tree.getProperty("state", "").toString());
}

void BlockFavorites::remove(const juce::File& file)
{
    if (file.isAChildOf(mDirectory) && file.moveToTrash() && onChanged)
        onChanged();
}

//==============================================================================
void BlockClipboard::copy(const BlockInstance& block)
{
    auto* plugin = block.getPlugin();

    if (plugin == nullptr)
        return;

    plugin->fillInPluginDescription(mDescription);
    mState.reset();
    plugin->getStateInformation(mState);
    mBypassed = block.isBypassed();
}

} // namespace blockrig
