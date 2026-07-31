#include "blocks/ir/IrLibrary.h"

namespace blockrig
{
namespace
{
bool looksLikeHash(const juce::String& text)
{
    return text.length() == 8 && text.containsOnly("0123456789abcdef");
}

juce::String nameFromFileName(const juce::File& file)
{
    const auto stem = file.getFileNameWithoutExtension();
    const auto tail = stem.fromLastOccurrenceOf(".", false, false);
    return looksLikeHash(tail) ? stem.upToLastOccurrenceOf(".", false, false) : stem;
}

juce::String hashOf(const juce::File& file)
{
    juce::MemoryBlock contents;
    file.loadFileAsData(contents);
    return juce::String::toHexString(
               juce::String::createStringFromData(contents.getData(),
                                                  static_cast<int>(contents.getSize()))
                   .hashCode64())
        .paddedLeft('0', 8);
}
} // namespace

IrLibrary::IrLibrary()
    : mDirectory(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                     .getChildFile("Application Support")
                     .getChildFile("BlockRig")
                     .getChildFile("IRs"))
{
    mDirectory.createDirectory();
}

juce::Array<IrLibrary::Entry> IrLibrary::getEntries() const
{
    juce::Array<Entry> entries;

    for (const auto& file : mDirectory.findChildFiles(juce::File::findFiles, true, getWildcard()))
    {
        const auto folder = file.getParentDirectory() == mDirectory
                                ? juce::String()
                                : file.getParentDirectory().getRelativePathFrom(mDirectory);

        entries.add(Entry{file, nameFromFileName(file), folder, file.getLastModificationTime()});
    }

    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.added > b.added; });

    return entries;
}

void IrLibrary::addIr(const juce::File& source, const juce::String& subfolder)
{
    if (!source.existsAsFile() || source.isAChildOf(mDirectory))
        return;

    const auto hash = hashOf(source);
    const auto shortHash = hash.substring(0, 8);

    for (const auto& existing : mDirectory.findChildFiles(juce::File::findFiles, true, getWildcard()))
    {
        const auto tail = existing.getFileNameWithoutExtension().fromLastOccurrenceOf(".", false, false);
        const auto existingHash = looksLikeHash(tail) ? tail : hashOf(existing).substring(0, 8);

        if (existingHash == shortHash)
            return;
    }

    auto directory = mDirectory;

    if (subfolder.isNotEmpty())
    {
        directory = directory.getChildFile(juce::File::createLegalFileName(subfolder));
        directory.createDirectory();
    }

    const auto target = directory.getChildFile(
        juce::File::createLegalFileName(source.getFileNameWithoutExtension()) + "." + shortHash
        + source.getFileExtension());

    if (source.copyFileTo(target) && onChanged)
        onChanged();
}

} // namespace blockrig
