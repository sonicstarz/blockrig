#include "blocks/nam/CaptureLibrary.h"

namespace blockrig
{
namespace
{
juce::String hashOf(const juce::String& content)
{
    // 64-bit content hash: for deduplicating a personal capture collection the
    // collision odds are negligible, and it avoids linking juce_cryptography.
    return juce::String::toHexString(content.hashCode64()).paddedLeft('0', 8);
}

/// The content hash rides in a sidecar-free way: "<name>.<hash8>.nam". Dedup
/// needs only a directory listing, no index file that can drift out of sync
/// with the files it describes.
///
/// Only a tail that actually looks like a hash counts. People also drop files
/// into the folder themselves, and "Nobels ODR-1 S1.5 D10.nam" must not have
/// its name cut at the dot nor its tail mistaken for a hash.
bool looksLikeHash(const juce::String& text)
{
    return text.length() == 8 && text.containsOnly("0123456789abcdef");
}

juce::String hashFromFileName(const juce::File& file)
{
    const auto tail = file.getFileNameWithoutExtension().fromLastOccurrenceOf(".", false, false);
    return looksLikeHash(tail) ? tail : juce::String();
}

juce::String nameFromFileName(const juce::File& file)
{
    const auto stem = file.getFileNameWithoutExtension();
    const auto tail = stem.fromLastOccurrenceOf(".", false, false);
    return looksLikeHash(tail) ? stem.upToLastOccurrenceOf(".", false, false) : stem;
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

    for (const auto& file : mDirectory.findChildFiles(juce::File::findFiles, true, "*.nam"))
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

bool CaptureLibrary::containsContent(const juce::String& hash) const
{
    const auto shortHash = hash.substring(0, 8);

    for (const auto& file : mDirectory.findChildFiles(juce::File::findFiles, true, "*.nam"))
    {
        auto fileHash = hashFromFileName(file);

        // Files people drop in themselves carry no hash in the name; hash the
        // content instead so they still deduplicate.
        if (fileHash.isEmpty())
            fileHash = hashOf(file.loadFileAsString()).substring(0, 8);

        if (fileHash == shortHash)
            return true;
    }

    return false;
}

juce::File CaptureLibrary::targetFileFor(const juce::String& name, const juce::String& hash,
                                         const juce::String& subfolder) const
{
    const auto safeName = juce::File::createLegalFileName(name.isNotEmpty() ? name : "Capture");

    auto directory = mDirectory;
    if (subfolder.isNotEmpty())
    {
        directory = directory.getChildFile(juce::File::createLegalFileName(subfolder));
        directory.createDirectory();
    }

    return directory.getChildFile(safeName + "." + hash.substring(0, 8) + ".nam");
}

void CaptureLibrary::addCapture(const juce::File& source, const juce::String& subfolder)
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

    if (source.copyFileTo(targetFileFor(source.getFileNameWithoutExtension(), hash, subfolder)))
        noteAdded();
}

void CaptureLibrary::addCaptureJson(const juce::String& json, const juce::String& name,
                                    const juce::String& subfolder)
{
    if (json.isEmpty())
        return;

    const auto hash = hashOf(json);
    if (containsContent(hash))
        return;

    if (targetFileFor(name, hash, subfolder).replaceWithText(json))
        noteAdded();
}

void CaptureLibrary::noteAdded()
{
    if (onChanged)
        onChanged();
}

} // namespace blockrig
