#include "host/PluginScannerWorker.h"

#include <cstdlib>

namespace blockrig
{
namespace
{
/// Extra time the child allows beyond the coordinator's own deadline, so the
/// coordinator gets to denylist the plug-in before we vanish.
constexpr int kSelfWatchdogMarginMs = 5000;
} // namespace

PluginScannerWorker::SelfWatchdog::SelfWatchdog()
    : juce::Thread("Scanner self-watchdog")
{
    startThread(juce::Thread::Priority::low);
}

PluginScannerWorker::SelfWatchdog::~SelfWatchdog()
{
    stopThread(1000);
}

void PluginScannerWorker::SelfWatchdog::probeStarted(int allowedMs)
{
    mAllowedMs.store(allowedMs + kSelfWatchdogMarginMs, std::memory_order_relaxed);
    mProbeStartMs.store(juce::Time::currentTimeMillis(), std::memory_order_relaxed);
}

void PluginScannerWorker::SelfWatchdog::probeFinished()
{
    mProbeStartMs.store(0, std::memory_order_relaxed);
}

void PluginScannerWorker::SelfWatchdog::run()
{
    while (!threadShouldExit())
    {
        const auto started = mProbeStartMs.load(std::memory_order_relaxed);
        const auto allowed = mAllowedMs.load(std::memory_order_relaxed);

        if (started != 0 && allowed > 0 && juce::Time::currentTimeMillis() - started > allowed)
        {
            // The message thread is wedged inside third-party code, so there is
            // nothing to unwind safely: leave immediately rather than linger as
            // an orphan. The coordinator has already denylisted this plug-in.
            std::_Exit(0);
        }

        wait(250);
    }
}

PluginScannerWorker::PluginScannerWorker()
{
    // Built-in blocks are deliberately not registered here: they need no
    // scanning, and keeping them out means a scanner child pulls in less code.
    juce::addDefaultFormatsToManager(mFormats);
}

PluginScannerWorker::~PluginScannerWorker()
{
    cancelPendingUpdate();
}

bool PluginScannerWorker::initialiseFromCommandLine(const juce::String& commandLine)
{
    return juce::ChildProcessWorker::initialiseFromCommandLine(commandLine, kProcessUid);
}

void PluginScannerWorker::handleMessageFromCoordinator(const juce::MemoryBlock& block)
{
    if (block.isEmpty())
        return;

    const std::lock_guard<std::mutex> lock(mMutex);

    // Some formats insist on being probed from the message thread. If we can
    // scan right here, do; otherwise defer to an async callback on that thread.
    if (const auto results = scan(block); !results.isEmpty())
    {
        sendResults(results);
    }
    else
    {
        mPendingBlocks.emplace(block);
        triggerAsyncUpdate();
    }
}

void PluginScannerWorker::handleConnectionLost()
{
    if (onConnectionLost)
        onConnectionLost();
}

void PluginScannerWorker::handleAsyncUpdate()
{
    for (;;)
    {
        const std::lock_guard<std::mutex> lock(mMutex);

        if (mPendingBlocks.empty())
            return;

        sendResults(scan(mPendingBlocks.front()));
        mPendingBlocks.pop();
    }
}

juce::OwnedArray<juce::PluginDescription> PluginScannerWorker::scan(const juce::MemoryBlock& block)
{
    juce::MemoryInputStream stream{block, false};
    const auto formatName = stream.readString();
    const auto identifier = stream.readString();
    // The coordinator passes its own deadline so our self-watchdog can align
    // with it instead of guessing.
    const auto allowedMs = stream.readInt();

    juce::PluginDescription description;
    description.fileOrIdentifier = identifier;
    description.uniqueId = description.deprecatedUid = 0;

    juce::AudioPluginFormat* matchingFormat = nullptr;
    for (auto* format : mFormats.getFormats())
        if (format->getName() == formatName)
            matchingFormat = format;

    juce::OwnedArray<juce::PluginDescription> results;

    if (matchingFormat != nullptr
        && (juce::MessageManager::getInstance()->isThisTheMessageThread()
            || matchingFormat->requiresUnblockedMessageThreadDuringCreation(description)))
    {
        // This is the call that can crash or hang on a hostile plug-in, which is
        // exactly why it happens over here — and why the watchdog is armed
        // around it rather than around the whole process.
        mWatchdog.probeStarted(allowedMs);
        matchingFormat->findAllTypesForFile(results, identifier);
        mWatchdog.probeFinished();
    }

    return results;
}

void PluginScannerWorker::sendResults(const juce::OwnedArray<juce::PluginDescription>& results)
{
    juce::XmlElement list("LIST");

    for (const auto* description : results)
        list.addChildElement(description->createXml().release());

    const auto text = list.toString();
    sendMessageToCoordinator({text.toRawUTF8(), text.getNumBytesAsUTF8()});
}

} // namespace blockrig
