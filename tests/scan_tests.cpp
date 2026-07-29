// Scans this machine's real plug-in corpus out-of-process and checks that
// misbehaving plug-ins are contained rather than fatal.
//
// This binary plays both roles: run normally it is the coordinator, and when the
// coordinator relaunches it with the scanner UID it becomes the child that
// actually loads plug-ins. That is exactly the shipping arrangement, so the
// crash-containment path is the one under test rather than a stand-in.

#include <cstdio>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "host/PluginCatalog.h"
#include "host/PluginScannerWorker.h"

namespace
{
int gFailures = 0;

void check(bool condition, const juce::String& what)
{
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what.toRawUTF8());
    if (!condition)
        ++gFailures;
}

/// If this process was launched as a scanner child, serve that role and exit.
bool runAsScannerChildIfRequested(const juce::String& commandLine)
{
    auto worker = std::make_unique<blockrig::PluginScannerWorker>();

    if (!worker->initialiseFromCommandLine(commandLine))
        return false;

    // Keep the child alive until the parent disconnects. No JUCEApplication
    // here, so the message loop is driven directly.
    bool keepGoing = true;
    worker->onConnectionLost = [&keepGoing] { keepGoing = false; };

    while (keepGoing)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    return true;
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray arguments;
    for (int i = 1; i < argc; ++i)
        arguments.add(juce::String(argv[i]));
    const auto commandLine = arguments.joinIntoString(" ");

    if (runAsScannerChildIfRequested(commandLine))
        return 0;

    std::printf("Coordinator: %s\n",
                juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName().toRawUTF8());

    // Scan into a scratch directory so a test run never disturbs real settings.
    const auto storage = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("BlockRigScanTest");
    storage.deleteRecursively();

    blockrig::PluginCatalog catalog;
    catalog.setStorageDirectory(storage);
    catalog.loadFromStorage();

    std::printf("\nBuilt-in blocks\n");
    const auto builtIns = catalog.getBuiltInDescriptions();
    check(!builtIns.isEmpty(), "catalog exposes built-in blocks");
    for (const auto& description : builtIns)
        std::printf("       %s (%s)\n", description.name.toRawUTF8(), description.pluginFormatName.toRawUTF8());

    std::printf("\nFormats available\n");
    juce::StringArray formatNames;
    for (auto* format : catalog.getFormatManager().getFormats())
        formatNames.add(format->getName() + (format->canScanForPlugins() ? "" : " (no scan)"));
    std::printf("       %s\n", formatNames.joinIntoString(", ").toRawUTF8());
    check(formatNames.size() >= 3, "AudioUnit, VST3 and the built-in format are all present");

    // A short timeout keeps the test quick; shipping default is 60 s.
    catalog.setPerPluginTimeout(juce::RelativeTime::seconds(20.0));

    std::printf("\nScanning (out of process)\n");
    const auto startTime = juce::Time::getCurrentTime();
    int lastReported = 0;

    const auto summary = catalog.scanAllFormats([&lastReported](const blockrig::PluginCatalog::ScanProgress& progress) {
        // Keep the log readable on a corpus this size.
        if (progress.scanned - lastReported >= 25 || progress.scanned == progress.total)
        {
            lastReported = progress.scanned;
            std::printf("       %d/%d scanned, %d found  (%s)\n", progress.scanned, progress.total, progress.found,
                        progress.currentPluginName.toRawUTF8());
            std::fflush(stdout);
        }
    });

    const auto elapsed = juce::Time::getCurrentTime() - startTime;

    std::printf("\nResults\n");
    std::printf("       scanned %d, found %d types, denylisted %d, timed out %d, in %.1f s\n", summary.scanned,
                summary.found, summary.denylisted, summary.timedOut, elapsed.inSeconds());

    check(summary.scanned > 0, "the scan actually probed plug-ins");
    check(summary.found > 0, "the scan found usable plug-ins");

    // The whole point: hostile plug-ins must not be fatal. Reaching this line at
    // all means no crash in a child took the coordinator with it.
    check(true, "coordinator survived the full corpus scan");

    if (!summary.denylistedNames.isEmpty())
    {
        std::printf("\n       denylisted:\n");
        for (const auto& name : summary.denylistedNames)
            std::printf("         %s\n", name.toRawUTF8());
    }

    std::printf("\nPersistence\n");
    check(catalog.getKnownPluginList().createXml() != nullptr, "plugin list is serialisable");

    // A fresh catalog must see the same plug-ins without rescanning.
    {
        blockrig::PluginCatalog reloaded;
        reloaded.setStorageDirectory(storage);
        reloaded.loadFromStorage();

        const int reloadedCount = reloaded.getKnownPluginList().getNumTypes();
        std::printf("       reloaded %d types (scanned %d)\n", reloadedCount,
                    catalog.getKnownPluginList().getNumTypes());
        check(reloadedCount == catalog.getKnownPluginList().getNumTypes(),
              "a fresh catalog reloads the same plug-ins from disk");
        check(reloaded.getDenylist().size() == catalog.getDenylist().size(), "denylist persists across restarts");
    }

    std::printf("\nDead man's pedal\n");
    // After a clean scan the pedal file must not still be accusing a plug-in.
    const auto pedal = catalog.getDeadMansPedalFile();
    const auto pedalContents = pedal.existsAsFile() ? pedal.loadFileAsString().trim() : juce::String();
    std::printf("       pedal file %s, contents %s\n", pedal.existsAsFile() ? "exists" : "absent",
                pedalContents.isEmpty() ? "(empty)" : pedalContents.toRawUTF8());
    check(pedalContents.isEmpty(), "pedal file is clear after a completed scan");

    std::printf("\nDenylist handling\n");
    const int denylistBefore = catalog.getDenylist().size();
    catalog.getKnownPluginList().addToBlacklist("/fake/path/Hostile.vst3");
    check(catalog.getDenylist().size() == denylistBefore + 1, "a plug-in can be denylisted");
    catalog.clearDenylist();
    check(catalog.getDenylist().isEmpty(), "denylist can be cleared by the user");

    // A hung plug-in wedges its child's message thread, so neither the
    // coordinator's kill message nor JUCE's connection-lost notification can
    // reach it. Without the child's own watchdog those children outlive the app.
    std::printf("\nNo orphaned scanner children\n");
    {
        // The bracket stops the pattern matching pgrep's own argv, and children
        // exit asynchronously, so allow a grace period rather than demanding
        // they be gone the instant the scan returns.
        const auto uid = juce::String(blockrig::PluginScannerWorker::kProcessUid);
        const auto pattern = "[" + uid.substring(0, 1) + "]" + uid.substring(1);

        const auto countSurvivors = [&pattern] {
            juce::ChildProcess pgrep;
            if (!pgrep.start("pgrep -f " + pattern))
                return -1;

            const auto output = pgrep.readAllProcessOutput().trim();
            pgrep.waitForProcessToFinish(5000);
            return output.isEmpty() ? 0 : juce::StringArray::fromLines(output).size();
        };

        int count = countSurvivors();
        const auto deadline = juce::Time::getMillisecondCounter() + 15000;

        while (count > 0 && juce::Time::getMillisecondCounter() < deadline)
        {
            juce::Thread::sleep(500);
            count = countSurvivors();
        }

        std::printf("       surviving scanner processes: %d\n", count);
        check(count == 0, "every scanner child exited, including the ones that hung");
    }

    storage.deleteRecursively();

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", gFailures,
                gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
