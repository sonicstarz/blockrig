#include "blocks/nam/CaptureLibrary.h"

namespace blockrig
{
namespace
{
juce::String hashOf(const juce::String& content)
{
    // 64-bit content hash: for deduplicating a personal capture collection the
    // collision odds are negligible, and it avoids linking juce_cryptography.
    return juce::String::toHexString(content.hashCode64());
}

/// The content hash rides in a sidecar-free way: "<name>.<hash8>.nam". Dedup
/// needs only a directory listing, no index file that can drift out of sync
/// with the files it describes.
juce::String hashFromFileName(const juce::File& file)
{
    const auto stem = file.getFileNameWithoutExtension(); // "<name>.<hash8>"
    return stem.fromLastOccurrenceOf(".", false, false);
}

juce::String nameFromFileName(const juce::File& file)
{
    return file.getFileNameWithoutExtension().upToLastOccurrenceOf(".", false, false);
}
} // namespace

CaptureLibrary::CaptureLibrary()
    : mDirectory(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                     .getChildFile("Application Support")
                     .getChildFile("BlockRig")
                     .getChildFile("Captures"))
{
    mDirectory.createDirectory();
}

juce::Array<CaptureLibrary::Entry> CaptureLibrary::getEntries() const
{
    juce::Array<Entry> entries;

    for (const auto& file : mDirectory.findChildFiles(juce::File::findFiles, false, "*.nam"))
        entries.add(Entry{file, nameFromFileName(file), file.getLastModificationTime()});

    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.added > b.added; });

    return entries;
}

bool CaptureLibrary::containsContent(const juce::String& hash) const
{
    const auto shortHash = hash.substring(0, 8);

    for (const auto& file : mDirectory.findChildFiles(juce::File::findFiles, false, "*.nam"))
        if (hashFromFileName(file) == shortHash)
            return true;

    return false;
}

juce::File CaptureLibrary::targetFileFor(const juce::String& name, const juce::String& hash) const
{
    const auto safeName = juce::File::createLegalFileName(name.isNotEmpty() ? name : "Capture");
    return mDirectory.getChildFile(safeName + "." + hash.substring(0, 8) + ".nam");
}

void CaptureLibrary::addCapture(const juce::File& source)
{
    if (!source.existsAsFile())
        return;

    // Skip anything already inside the library, or it would re-add itself with
    // every load from its own list.
    if (source.isAChildOf(mDirectory))
        return;

    const auto content = source.loadFileAsString();
    if (content.isEmpty())
        return;

    const auto hash = hashOf(content);
    if (containsContent(hash))
        return;

    if (source.copyFileTo(targetFileFor(source.getFileNameWithoutExtension(), hash)))
        noteAdded();
}

void CaptureLibrary::addCaptureJson(const juce::String& json, const juce::String& name)
{
    if (json.isEmpty())
        return;

    const auto hash = hashOf(json);
    if (containsContent(hash))
        return;

    if (targetFileFor(name, hash).replaceWithText(json))
        noteAdded();
}

void CaptureLibrary::noteAdded()
{
    if (onChanged)
        onChanged();
}

} // namespace blockrig
