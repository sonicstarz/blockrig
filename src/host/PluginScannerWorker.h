#pragma once

#include <functional>
#include <mutex>
#include <queue>

#include <juce_audio_processors/juce_audio_processors.h>

namespace blockrig
{

/// Worker half of out-of-process plug-in scanning.
///
/// The app relaunches its own executable with a magic command-line argument; in
/// that child process this class takes over, probes one plug-in at a time on
/// behalf of the parent, and returns the descriptions it finds. A plug-in that
/// crashes on load therefore takes down a disposable child instead of the app.
///
/// Modelled on JUCE's AudioPluginHost, but decoupled from JUCEApplication so it
/// works in a console binary too (which is what makes it testable).
class PluginScannerWorker final : private juce::ChildProcessWorker
                                , private juce::AsyncUpdater
{
public:
    /// Must match between parent and child, and be unique to this app.
    static constexpr const char* kProcessUid = "blockrig_scanner";

    PluginScannerWorker();
    ~PluginScannerWorker() override;

    /// Returns true if this process was launched as a scanner and has taken the
    /// role; the caller should then just run a message loop until told to stop.
    bool initialiseFromCommandLine(const juce::String& commandLine);

    /// Invoked when the parent goes away, so the host can end its message loop.
    std::function<void()> onConnectionLost;

private:
    /// Hard-exits this process if a single probe runs too long.
    ///
    /// Needed because JUCE cannot rescue us: the coordinator's kill is a message
    /// over the pipe, and the connection-lost notification is delivered via
    /// triggerAsyncUpdate — both of which need the message thread, which is
    /// precisely the thread stuck inside the hung plug-in. Without this, a hung
    /// child outlives the app as an orphan (observed with Waves shell plug-ins).
    class SelfWatchdog final : private juce::Thread
    {
    public:
        SelfWatchdog();
        ~SelfWatchdog() override;

        void probeStarted(int allowedMs);
        void probeFinished();

    private:
        void run() override;

        std::atomic<juce::int64> mProbeStartMs{0}; // 0 means idle
        std::atomic<int> mAllowedMs{0};
    };

    void handleMessageFromCoordinator(const juce::MemoryBlock& block) override;
    void handleConnectionLost() override;
    void handleAsyncUpdate() override;

    juce::OwnedArray<juce::PluginDescription> scan(const juce::MemoryBlock& block);
    void sendResults(const juce::OwnedArray<juce::PluginDescription>& results);

    std::mutex mMutex;
    std::queue<juce::MemoryBlock> mPendingBlocks;
    juce::AudioPluginFormatManager mFormats;
    SelfWatchdog mWatchdog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginScannerWorker)
};

} // namespace blockrig
