#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

namespace blockrig
{

/// Knows every block the user can add: the built-in ones plus every VST3 and
/// AudioUnit installed on the machine.
///
/// Scanning is always out-of-process, because loading an arbitrary plug-in means
/// running arbitrary third-party code. Three layers of containment:
///   * a child process does the probing, so a crash costs a disposable process
///   * a dead man's pedal file records the plug-in being probed, so a crash that
///     kills the child still gets that plug-in denylisted afterwards
///   * a per-plug-in watchdog kills the child when a plug-in *hangs* rather than
///     crashes, which JUCE's own example does not handle (its response loop
///     spins forever on timeout)
class PluginCatalog
{
public:
    PluginCatalog();
    ~PluginCatalog();

    /// Where the plug-in list, denylist and dead man's pedal live. Chosen by the
    /// app so it can pick a location a sandboxed DAW can still read.
    void setStorageDirectory(const juce::File& directory);

    /// Applies any denylisting implied by a previous run that died mid-scan.
    void loadFromStorage();
    void saveToStorage() const;

    juce::AudioPluginFormatManager& getFormatManager() { return mFormats; }
    juce::KnownPluginList& getKnownPluginList() { return mKnownPlugins; }

    /// Built-in blocks (the NAM). Kept out of the scanned list so a rescan can
    /// never denylist them, but anything enumerating "what can I add" needs both.
    const juce::Array<juce::PluginDescription>& getBuiltIns() const { return mBuiltIns; }
    const juce::KnownPluginList& getKnownPluginList() const { return mKnownPlugins; }

    /// Descriptions of the built-in blocks, which never need scanning.
    juce::Array<juce::PluginDescription> getBuiltInDescriptions() const;

    struct ScanProgress
    {
        juce::String currentPluginName;
        int scanned = 0;
        int total = 0;
        int found = 0;
    };

    struct ScanSummary
    {
        int scanned = 0;
        int found = 0;
        int denylisted = 0;
        int timedOut = 0;
        juce::StringArray denylistedNames;
        /// Looked like plug-ins but would not open — distinct from a crash.
        juce::StringArray failedNames;
    };

    /// Blocking scan of every scannable format. Calls `onProgress` on the calling
    /// thread; return false from `shouldAbort` to stop early.
    ScanSummary scanAllFormats(std::function<void(const ScanProgress&)> onProgress = {},
                               std::function<bool()> shouldAbort = {});

    /// How long a single plug-in may take before its child is killed and the
    /// plug-in denylisted. Hostile plug-ins are known to hang for minutes.
    void setPerPluginTimeout(juce::RelativeTime timeout) { mPerPluginTimeout = timeout; }
    juce::RelativeTime getPerPluginTimeout() const { return mPerPluginTimeout; }

    /// Debug escape hatch. Scanning in-process is never shipped behaviour.
    void setScanInProcess(bool shouldScanInProcess);

    juce::StringArray getDenylist() const { return mKnownPlugins.getBlacklistedFiles(); }
    void clearDenylist();

    juce::File getDeadMansPedalFile() const;

private:
    class Superprocess;
    class Scanner;

    juce::File getPluginListFile() const;

    juce::AudioPluginFormatManager mFormats;
    juce::KnownPluginList mKnownPlugins;
    juce::Array<juce::PluginDescription> mBuiltIns;

    juce::File mStorageDirectory;
    juce::RelativeTime mPerPluginTimeout = juce::RelativeTime::seconds(60.0);
    bool mScanInProcess = false;

    /// Owned by mKnownPlugins once installed; kept here so the scan summary can
    /// read back its timeout counters (KnownPluginList has no getter).
    Scanner* mScanner = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginCatalog)
};

} // namespace blockrig
