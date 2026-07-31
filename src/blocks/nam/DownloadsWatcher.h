#pragma once

#include <juce_events/juce_events.h>

#include "blocks/nam/CaptureLibrary.h"

namespace blockrig
{

/// Watches ~/Downloads and imports NAM captures into the library on arrival.
///
/// The zero-setup path to TONE3000: browse the site in any browser, hit
/// download, and the capture is in BlockRig's library seconds later - .nam
/// files directly, and .zip packs get their .nam entries pulled out. Only files
/// newer than the last sweep are considered, so deleting a capture from the
/// library does not resurrect it from an old download, and nothing is ever
/// moved or deleted in Downloads - it is the user's folder.
///
/// Sweeps run on their own thread, and that is load-bearing: the first touch of
/// ~/Downloads makes macOS put up a folder-access consent prompt that blocks
/// the calling thread until answered. On the message thread that froze the
/// entire app at 5 seconds after first launch, boot screen and all.
class DownloadsWatcher final : private juce::Thread
{
public:
    explicit DownloadsWatcher(const juce::File& stateFile);
    ~DownloadsWatcher() override;

private:
    void run() override;
    void sweep();
    void importZip(const juce::File& zipFile);

    juce::SharedResourcePointer<CaptureLibrary> mLibrary;
    juce::File mStateFile;
    juce::Time mLastSweep;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DownloadsWatcher)
};

} // namespace blockrig
