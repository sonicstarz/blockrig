#include "blocks/nam/DownloadsWatcher.h"

namespace blockrig
{
namespace
{
constexpr const char* kImportFolder = "Imported";
} // namespace

DownloadsWatcher::DownloadsWatcher(const juce::File& stateFile)
    : juce::Thread("Downloads watcher")
    , mStateFile(stateFile)
{
    // First run starts from "now": a Downloads folder full of years-old .nam
    // files should not be bulk-imported without being asked.
    const auto stored = mStateFile.loadFileAsString().trim();
    mLastSweep = stored.isNotEmpty()
                     ? juce::Time(stored.getLargeIntValue())
                     : juce::Time::getCurrentTime();

    startThread();
}

DownloadsWatcher::~DownloadsWatcher()
{
    stopThread(4000);
}

void DownloadsWatcher::run()
{
    while (!threadShouldExit())
    {
        sweep();

        for (int waited = 0; waited < 5000 && !threadShouldExit(); waited += 500)
            wait(500);
    }
}

void DownloadsWatcher::sweep()
{
    const auto downloads = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                               .getChildFile("Downloads");

    if (!downloads.isDirectory())
        return;

    const auto sweepStarted = juce::Time::getCurrentTime();
    bool anyImported = false;

    for (const auto& file : downloads.findChildFiles(juce::File::findFiles, false, "*.nam"))
    {
        if (file.getLastModificationTime() <= mLastSweep)
            continue;

        // A file still being written has a moving modification time; skip it
        // until it has been still for a couple of seconds.
        if (sweepStarted - file.getLastModificationTime() < juce::RelativeTime::seconds(2.0))
            continue;

        mLibrary->addCapture(file, kImportFolder);
        anyImported = true;
    }

    for (const auto& file : downloads.findChildFiles(juce::File::findFiles, false, "*.zip"))
    {
        if (file.getLastModificationTime() <= mLastSweep)
            continue;

        if (sweepStarted - file.getLastModificationTime() < juce::RelativeTime::seconds(2.0))
            continue;

        importZip(file);
        anyImported = true;
    }

    // Advance the watermark only past files old enough to have been considered,
    // so the settling window above cannot skip a file forever.
    mLastSweep = sweepStarted - juce::RelativeTime::seconds(2.0);
    mStateFile.replaceWithText(juce::String(mLastSweep.toMilliseconds()));

    juce::ignoreUnused(anyImported);
}

void DownloadsWatcher::importZip(const juce::File& zipFile)
{
    juce::ZipFile zip(zipFile);

    // Capture packs come as zips; anything else in them (IRs, readmes) is left
    // alone. The pack's name becomes a library subfolder when it has several.
    juce::Array<int> namEntries;

    for (int i = 0; i < zip.getNumEntries(); ++i)
        if (zip.getEntry(i)->filename.endsWithIgnoreCase(".nam"))
            namEntries.add(i);

    if (namEntries.isEmpty())
        return;

    const auto subfolder = namEntries.size() > 1
                               ? zipFile.getFileNameWithoutExtension()
                               : juce::String(kImportFolder);

    for (const auto index : namEntries)
    {
        std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(index));
        if (stream == nullptr)
            continue;

        const auto* entry = zip.getEntry(index);
        const auto name = juce::File(entry->filename).getFileNameWithoutExtension();

        mLibrary->addCaptureJson(stream->readEntireStreamAsString(), name, subfolder);
    }
}

} // namespace blockrig
