#include <thread>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "BlockRigProcessor.h"
#include "host/BlockInstance.h"
#include "host/PluginCatalog.h"
#include "blocks/nam/DownloadsWatcher.h"
#include "host/PluginScannerWorker.h"
#include "state/RigFiles.h"
#include "ui/AppShell.h"
#include "ui/MainView.h"

namespace blockrig
{
namespace
{
constexpr const char* kSettingsFileName = "BlockRig.settings";
/// The rig as it was when the app last closed, restored on the next launch.
constexpr const char* kSessionFileName = "LastSession.blockrig";
constexpr const char* kDeviceStateKey = "audioDeviceState";
} // namespace

/// The standalone app.
///
/// A plain GUI app rather than JUCE's StandaloneFilterWindow, because the audio
/// device *is* part of this product — the Input and Output blocks at the ends of
/// the lane show it — and because main() has to be able to become a plug-in
/// scanner child before any of the UI exists.
class StandaloneApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "BlockRig"; }
    const juce::String getApplicationVersion() override { return "0.2.0"; }
    /// Must be true. Plug-in scanning relaunches *this* executable as a child
    /// process for each plug-in, and JUCE's single-instance guard would kill
    /// those children — which silently breaks scanning entirely.
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        // Before anything else: this process may have been relaunched by our own
        // scanner to probe a plug-in, in which case it must not open a window.
        mScannerWorker = std::make_unique<PluginScannerWorker>();

        if (mScannerWorker->initialiseFromCommandLine(commandLine))
        {
            mScannerWorker->onConnectionLost = [this] { quit(); };
            return;
        }

        mScannerWorker.reset();

        // Headless scan, for setting the catalog up from a terminal and for
        // testing the very relaunch path the UI depends on.
        if (commandLine.contains("--scan"))
        {
            runHeadlessScan();
            return;
        }

        // Reports what the audio device is actually giving us, which is the only
        // way to tell "no signal" apart from "no input channel enabled".
        if (commandLine.contains("--audio-check"))
        {
            runAudioCheck();
            return;
        }

        // Reports what a hosted plug-in negotiates and whether it really produces
        // stereo, which no amount of listening can pin down precisely.
        if (commandLine.contains("--plugin-check"))
        {
            runPluginCheck(commandLine.fromFirstOccurrenceOf("--plugin-check", false, false).trim());
            return;
        }

        // Measures the whole path - block, chain, processor - rather than a
        // block in isolation, which is where a stereo image actually gets lost.
        if (commandLine.contains("--chain-check"))
        {
            runChainCheck(commandLine.fromFirstOccurrenceOf("--chain-check", false, false).trim());
            return;
        }

        mProcessor = std::make_unique<BlockRigProcessor>();
        mProcessor->setFollowsHostTransport(false); // there is no host out here
        mProcessor->getCatalog().setStorageDirectory(getStorageDirectory());
        mProcessor->getCatalog().loadFromStorage();

        // The app opens a live input into a live output, so start silent and let
        // the user commit. (wrapperType cannot tell us we are the app: this
        // processor is built directly, not through a plug-in wrapper.)
        mProcessor->setMuted(true);

        setUpAudio();

        // A rig carries its audio setup, so loading one puts the whole session
        // back - not just the blocks.
        mProcessor->getAudioStateXml = [this] {
            if (auto xml = mDeviceManager.createStateXml())
                return xml->toString();
            return juce::String();
        };
        mProcessor->applyAudioStateXml = [this](const juce::String& text) {
            if (const auto xml = juce::parseXML(text))
            {
                mDeviceManager.initialise(1, 2, xml.get(), true);
                ensureAnInputIsEnabled();
            }
        };

        mMainWindow = std::make_unique<MainWindow>(getApplicationName(), *mProcessor, mDeviceManager);
        mMainWindow->getShell().beginBoot();

        // Crash insurance only now: rigs are explicit files the user saves. This
        // keeps at most half a minute of unsaved work recoverable from disk.
        mAutoSave = std::make_unique<AutoSave>(*mProcessor, getSessionFile());

        // Captures downloaded in any browser flow into the library by themselves.
        mDownloadsWatcher = std::make_unique<DownloadsWatcher>(
            getStorageDirectory().getChildFile("DownloadsWatcher.state"));
    }

    void shutdown() override
    {
        // Save the rig first: everything below tears down what it describes.
        if (mProcessor != nullptr && mMainWindow != nullptr)
        {
            juce::String error;
            rigfiles::save(*mProcessor, getSessionFile(), error);
        }

        mAutoSave = nullptr;
        mDownloadsWatcher = nullptr;

        if (mScanThread.joinable())
            mScanThread.join();

        if (mCheckThread.joinable())
            mCheckThread.join();

        mCatalogForScan = nullptr;
        mMainWindow = nullptr;

        if (mProcessor != nullptr)
        {
            saveDeviceState();
            mProcessor->getCatalog().saveToStorage();
            mDeviceManager.removeMidiInputDeviceCallback({}, &mProcessor->getMidiEngine());
            mPlayer.setProcessor(nullptr);
            mDeviceManager.removeAudioCallback(&mPlayer);
        }

        mStatusLogger = nullptr;
        mProcessor = nullptr;
        mScannerWorker = nullptr;
    }

    void systemRequestedQuit() override
    {
        // The rig screen may have unsaved changes; it gets to ask first.
        if (mMainWindow != nullptr)
            mMainWindow->getShell().requestQuit([] { juce::JUCEApplication::getInstance()->quit(); });
        else
            quit();
    }

private:
    /// Somewhere both the app and a DAW-sandboxed plug-in instance can read.
    static juce::File getStorageDirectory()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Application Support")
            .getChildFile("BlockRig");
    }

    static juce::File getSettingsFile() { return getStorageDirectory().getChildFile(kSettingsFileName); }
    static juce::File getSessionFile() { return getStorageDirectory().getChildFile(kSessionFileName); }

    /// Kept for crash recovery: LastSession.blockrig can be imported from the
    /// rig menu. No longer auto-restored - the home screen owns what opens.
    void restoreLastSession()
    {
        const auto session = getSessionFile();

        if (!session.existsAsFile())
            return;

        rigfiles::load(*mProcessor, session, [this](rigstate::RestoreResult result, juce::String error) {
            if (error.isNotEmpty())
            {
                juce::Logger::writeToLog("Could not restore the last session: " + error);
                return;
            }

            if (!result.missingPlugins.isEmpty())
                juce::Logger::writeToLog("Session restored without: "
                                         + result.missingPlugins.joinIntoString(", "));

            if (auto* view = dynamic_cast<MainView*>(mMainWindow->getContentComponent()))
                view->rigWasRestored();
        });
    }

    /// Writes the session periodically so a crash does not cost the whole rig.
    class AutoSave final : private juce::Timer
    {
    public:
        AutoSave(BlockRigProcessor& processor, juce::File file)
            : mProcessor(processor)
            , mFile(std::move(file))
        {
            startTimer(30000);
        }

        ~AutoSave() override { stopTimer(); }

    private:
        void timerCallback() override
        {
            juce::String error;
            rigfiles::save(mProcessor, mFile, error);
        }

        BlockRigProcessor& mProcessor;
        juce::File mFile;
    };

    void setUpAudio()
    {
        std::unique_ptr<juce::XmlElement> savedState;

        if (const auto settings = juce::parseXML(getSettingsFile()))
            if (auto* wrapper = settings->getChildByName(kDeviceStateKey))
                if (auto* deviceState = wrapper->getFirstChildElement())
                    savedState = std::make_unique<juce::XmlElement>(*deviceState);

        // Guitar in, stereo out: one input channel is the common case, so do not
        // demand two. Asking for two made initialise treat any saved setup on a
        // one-input interface as unsatisfiable, so it fell back to the default
        // input device on every launch.
        mDeviceManager.initialise(1, 2, savedState.get(), true);

        ensureAnInputIsEnabled();

        mPlayer.setProcessor(mProcessor.get());
        mDeviceManager.addAudioCallback(&mPlayer);

        // Every MIDI input feeds the engine. Enabling them all is what a floor
        // unit does; nobody wants to find an "enable controller" checkbox on a
        // dark stage.
        for (const auto& input : juce::MidiInput::getAvailableDevices())
            mDeviceManager.setMidiInputDeviceEnabled(input.identifier, true);

        mDeviceManager.addMidiInputDeviceCallback({}, &mProcessor->getMidiEngine());

        logAudioStatus();
    }

    /// Records what the audio device actually did at startup. Without this the
    /// only way to tell "device failed to open" from "signal is just quiet" in
    /// the running app is to guess.
    void logAudioStatus()
    {
        const auto logFile = getStorageDirectory().getChildFile("AudioStatus.log");
        juce::StringArray lines;

        lines.add("BlockRig audio status  " + juce::Time::getCurrentTime().toString(true, true));

        if (auto* device = mDeviceManager.getCurrentAudioDevice())
        {
            const auto activeIn = device->getActiveInputChannels();
            const auto activeOut = device->getActiveOutputChannels();

            lines.add("device      : " + device->getName());
            lines.add("open        : " + juce::String(device->isOpen() ? "yes" : "no"));
            lines.add("playing     : " + juce::String(device->isPlaying() ? "yes" : "no"));
            lines.add("sample rate : " + juce::String(device->getCurrentSampleRate()));
            lines.add("buffer      : " + juce::String(device->getCurrentBufferSizeSamples()));
            lines.add("inputs      : " + juce::String(activeIn.countNumberOfSetBits()) + " active of "
                      + juce::String(device->getInputChannelNames().size()));
            lines.add("outputs     : " + juce::String(activeOut.countNumberOfSetBits()) + " active of "
                      + juce::String(device->getOutputChannelNames().size()));

            juce::StringArray outNames;
            for (int i = 0; i < device->getOutputChannelNames().size(); ++i)
                if (activeOut[i])
                    outNames.add(juce::String(i + 1) + ":" + device->getOutputChannelNames()[i]);
            lines.add("active out  : " + outNames.joinIntoString(", "));
        }
        else
        {
            lines.add("device      : NONE — the app opened no audio device");
        }

        lines.add("processor   : " + juce::String(mProcessor->getTotalNumInputChannels()) + " in / "
                  + juce::String(mProcessor->getTotalNumOutputChannels()) + " out");
        lines.add("muted       : " + juce::String(mProcessor->isMuted() ? "yes" : "no"));

        getStorageDirectory().createDirectory();
        logFile.replaceWithText(lines.joinIntoString("\n") + "\n");

        // Keep sampling levels so the log shows whether audio is flowing.
        mStatusLogger = std::make_unique<StatusLogger>(*mProcessor, logFile, mDeviceManager);
    }

    /// Scans on a background thread, printing progress, then quits. Same code
    /// path the UI uses, so if this works the button works.
    void runHeadlessScan()
    {
        mCatalogForScan = std::make_unique<PluginCatalog>();
        mCatalogForScan->setStorageDirectory(getStorageDirectory());
        mCatalogForScan->loadFromStorage();

        mScanThread = std::thread([this] {
            int lastReported = 0;

            const auto summary = mCatalogForScan->scanAllFormats(
                [&lastReported](const PluginCatalog::ScanProgress& progress) {
                    if (progress.scanned - lastReported >= 20 || progress.scanned == progress.total)
                    {
                        lastReported = progress.scanned;
                        std::printf("%d/%d scanned, %d found  (%s)\n", progress.scanned, progress.total,
                                    progress.found, progress.currentPluginName.toRawUTF8());
                        std::fflush(stdout);
                    }
                });

            std::printf("\nDone: %d scanned, %d found, %d denylisted, %d timed out\n", summary.scanned,
                        summary.found, summary.denylisted, summary.timedOut);

            for (const auto& name : summary.denylistedNames)
                std::printf("  skipped: %s\n", name.toRawUTF8());

            std::fflush(stdout);
            juce::MessageManager::callAsync([] { juce::JUCEApplication::getInstance()->quit(); });
        });
    }

    /// Appends live level readings to the status log a few times a second, so the
    /// running app's behaviour can be inspected from outside it.
    class StatusLogger final : private juce::Timer
    {
    public:
        StatusLogger(BlockRigProcessor& processor, juce::File file,
                     juce::AudioDeviceManager& deviceManager)
            : mProcessor(processor)
            , mFile(std::move(file))
            , mDeviceManager(deviceManager)
        {
            startTimer(500);
        }

        ~StatusLogger() override { stopTimer(); }

    private:
        void timerCallback() override
        {
            mPeakIn = juce::jmax(mPeakIn, mProcessor.getInputLevel());
            mPeakOut = juce::jmax(mPeakOut, mProcessor.getOutputLevel());

            if (++mTicks % 4 != 0)
                return;

            // The device usually opens after the header is written, because macOS
            // grants microphone access asynchronously. Reporting it once at
            // startup made every log say "no audio device" regardless.
            const auto deviceName = mDeviceManager.getCurrentAudioDevice() != nullptr
                                      ? mDeviceManager.getCurrentAudioDevice()->getName()
                                      : juce::String("none");

            if (deviceName != mLastDevice)
            {
                mLastDevice = deviceName;
                mFile.appendText("device now: " + deviceName + "   processor "
                                 + juce::String(mProcessor.getTotalNumInputChannels()) + " in / "
                                 + juce::String(mProcessor.getTotalNumOutputChannels()) + " out\n");
            }

            const auto blocks = mProcessor.getProcessBlockCount();

            const auto probe = mProcessor.getWidthProbe();

            mFile.appendText("bpm " + juce::String(mProcessor.getTransport().getBpm(), 2)
                             + (mProcessor.getTransport().isFollowingHost() ? " (host)" : "")
                             + "  levels: in " + juce::String(mPeakIn, 5) + "  out "
                             + juce::String(mPeakOut, 5) + "  L-R "
                             + juce::String(mProcessor.getStereoDifference(), 5) + "  muted "
                             + juce::String(mProcessor.isMuted() ? "yes" : "no") + "  processBlocks "
                             + juce::String(blocks) + " (+" + juce::String(blocks - mLastBlockCount) + ")"
                             + "  width[buf " + juce::String(probe.channels) + "ch  in "
                             + juce::String(probe.afterInput, 5) + "  chain "
                             + juce::String(probe.afterChain, 5) + "  out "
                             + juce::String(probe.atOutput, 5) + "]\n");

            // What each block actually negotiated, in the running app. The
            // harness kept reporting the right layouts while the app did not, so
            // the layouts have to be readable from here too.
            juce::StringArray layouts;

            for (auto* block : mProcessor.getChain().getBlocks())
                if (auto* plugin = block->getPlugin())
                    layouts.add(block->getDisplayName() + " " + juce::String(plugin->getTotalNumInputChannels())
                                + "/" + juce::String(plugin->getTotalNumOutputChannels())
                                + (block->getSourceIsMono() ? " fedMONO" : " fedST"));

            if (layouts.isEmpty())
                layouts.add("(empty)");

            if (layouts.joinIntoString(", ") != mLastLayouts)
            {
                mLastLayouts = layouts.joinIntoString(", ");
                mFile.appendText("chain[source " + juce::String(mProcessor.sourceIsMono() ? "MONO" : "stereo")
                                 + "]: " + mLastLayouts + "\n");
            }

            mLastBlockCount = blocks;
            mPeakIn = 0.0f;
            mPeakOut = 0.0f;
        }

        BlockRigProcessor& mProcessor;
        juce::File mFile;
        float mPeakIn = 0.0f, mPeakOut = 0.0f;
        int mTicks = 0;
        juce::int64 mLastBlockCount = 0;
        juce::String mLastLayouts;
        juce::AudioDeviceManager& mDeviceManager;
        juce::String mLastDevice;
    };

    std::unique_ptr<StatusLogger> mStatusLogger;

    /// Instantiates every catalogued plug-in matching `fragment` exactly as the
    /// chain would, and reports its negotiated channel layout and whether it
    /// actually produces a stereo image from a mono-identical input.
    /// Pushes decorrelated stereo through the real processor and reports what
    /// survives at each stage.
    ///
    /// --plugin-check exonerated every plug-in while the rig still came out mono,
    /// which means the block was never the thing to measure. This runs the same
    /// signal through BlockRigProcessor so the chain and the processor's own
    /// input handling are in the measurement too.
    void runChainCheck(const juce::String& fragment)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        mProcessor = std::make_unique<BlockRigProcessor>();
        mProcessor->getCatalog().setStorageDirectory(getStorageDirectory());
        mProcessor->getCatalog().loadFromStorage();
        mProcessor->setMuted(false);

        const auto describe = [](const juce::AudioBuffer<float>& buffer) {
            const int numSamples = buffer.getNumSamples();
            double left = 0.0, right = 0.0, difference = 0.0;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto l = buffer.getReadPointer(0)[i];
                const auto r = buffer.getReadPointer(1)[i];
                left += l * l;
                right += r * r;
                difference += std::abs(l - r);
            }

            return juce::String::formatted("L %.5f  R %.5f  L-R %.5f  %s",
                                           std::sqrt(left / numSamples), std::sqrt(right / numSamples),
                                           difference / numSamples,
                                           difference / numSamples > 1.0e-5 ? "STEREO" : "MONO");
        };

        // "--chain-check session" measures the rig the user actually has, which
        // is the only way to catch a collapse caused by one block among several.
        if (fragment.equalsIgnoreCase("session"))
        {
            juce::String loadError;
            bool finished = false;

            rigfiles::load(*mProcessor, getSessionFile(),
                           [&](rigstate::RestoreResult result, juce::String error) {
                               loadError = error;
                               if (!result.missingPlugins.isEmpty())
                                   std::printf("Missing: %s\n",
                                               result.missingPlugins.joinIntoString(", ").toRawUTF8());
                               finished = true;
                           });

            for (int i = 0; i < 400 && !finished; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

            if (loadError.isNotEmpty())
                std::printf("Could not load the session: %s\n", loadError.toRawUTF8());

            for (auto* block : mProcessor->getChain().getBlocks())
                std::printf("  %-30s %s\n", block->getDisplayName().toRawUTF8(),
                            block->isMonoOnly() ? "MONO-ONLY - sums the stereo image away" : "stereo");

            // Per-block, because "the rig is stereo" is not the same as knowing
            // which block widens it and which one squashes it back down.
            std::printf("\nWidth after each block, fed a hard-panned left-only signal:\n");

            juce::AudioBuffer<float> probe(2, blockSize);
            juce::MidiBuffer probeMidi;
            const auto blocks = mProcessor->getChain().getBlocks();

            for (auto* block : blocks)
                block->prepare(sampleRate, blockSize, block->getSourceIsMono());

            for (int pass = 0; pass <= 120; ++pass)
            {
                for (int channel = 0; channel < 2; ++channel)
                {
                    auto* data = probe.getWritePointer(channel);
                    for (int i = 0; i < blockSize; ++i)
                        data[i] = channel == 1 ? 0.0f
                                               : 0.25f * std::sin(0.07f * static_cast<float>(pass * blockSize + i));
                }

                const bool report = pass == 120;

                if (report)
                    std::printf("  %-30s %s\n", "(input)", describe(probe).toRawUTF8());

                for (auto* block : blocks)
                {
                    probeMidi.clear();
                    block->process(probe, probeMidi, blockSize / sampleRate);

                    if (report)
                        std::printf("  %-30s %s\n", block->getDisplayName().toRawUTF8(),
                                    describe(probe).toRawUTF8());
                }
            }

            std::printf("\n");
        }
        // Comma-separated, in order, so the effect of chain ORDER can be measured
        // rather than asserted - "put the delay after the amp" is worth nothing as
        // advice until the numbers back it.
        else if (fragment.isNotEmpty())
        {
            // Built-ins live in their own list, not the scanned catalogue -
            // without them "NAM" silently matched the old NAM Modeler VST3.
            auto types = mProcessor->getCatalog().getKnownPluginList().getTypes();
            for (const auto& builtIn : mProcessor->getCatalog().getBuiltIns())
                types.insert(0, builtIn);
            juce::StringArray wanted;
            wanted.addTokens(fragment, ",", "");
            wanted.trim();
            wanted.removeEmptyStrings();

            int stage = 0;

            for (const auto& want : wanted)
            {
                bool found = false;

                // Exact name first: "NAM" must find the built-in NAM block, not
                // the old "NAM Modeler" plug-in that happens to contain it.
                juce::Array<juce::PluginDescription> ordered;
                for (const auto& description : types)
                    if (description.name.equalsIgnoreCase(want))
                        ordered.add(description);
                for (const auto& description : types)
                    if (!description.name.equalsIgnoreCase(want)
                        && description.name.containsIgnoreCase(want))
                        ordered.add(description);

                for (const auto& description : ordered)
                {

                    std::printf("Adding %s\n", description.name.toRawUTF8());
                    mProcessor->addBlock(description, BlockPosition{stage, 0, 0, true});
                    found = true;
                    break;
                }

                if (!found)
                {
                    std::printf("Nothing in the catalogue matched \"%s\"\n", want.toRawUTF8());
                    continue;
                }

                // Instantiation is asynchronous, and the next block must land
                // after this one, so wait for each in turn.
                const int expected = stage + 1;
                for (int i = 0; i < 200 && mProcessor->getChain().getNumBlocks() < expected; ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

                ++stage;
            }
        }

        // Built-in blocks that CAN create width are driven here, so the width
        // probe proves the mechanism rather than only the neutral case.
        for (auto* block : mProcessor->getChain().getBlocks())
            if (auto* plugin = block->getPlugin(); plugin != nullptr && plugin->getName() == "Utility")
                for (auto* parameter : plugin->getParameters())
                    if (parameter->getName(16) == "Pan")
                    {
                        parameter->setValueNotifyingHost(0.0f); // hard left
                        std::printf("Set Utility / Pan hard left\n");
                    }

        // Answers "does a mono feed leave a stereo plug-in stuck in mono" with a
        // measurement rather than an opinion: drive the plug-in into an explicitly
        // stereo mode and see whether the output decorrelates.
        //
        // Sweeps the value rather than reading getAllValueStrings(), because a
        // VST3 parameter can present discrete text while reporting no discrete
        // choices - which is exactly what Valhalla's DelayStyle does.
        for (auto* block : mProcessor->getChain().getBlocks())
        {
            auto* plugin = block->getPlugin();
            if (plugin == nullptr)
                continue;

            for (auto* parameter : plugin->getParameters())
            {
                const auto name = parameter->getName(64);

                if (!name.containsIgnoreCase("style") && !name.containsIgnoreCase("mode"))
                    continue;

                juce::StringArray seen;

                for (int step = 0; step <= 64; ++step)
                {
                    const auto value = static_cast<float>(step) / 64.0f;
                    const auto text = parameter->getText(value, 64);

                    if (!seen.contains(text))
                        seen.add(text);

                    if (text.replace(" ", "").containsIgnoreCase("pingpong"))
                    {
                        parameter->setValueNotifyingHost(value);
                        std::printf("Set %s / %s to \"%s\"\n", plugin->getName().toRawUTF8(),
                                    name.toRawUTF8(), text.toRawUTF8());
                        break;
                    }
                }

                std::printf("  %s / %s offers: %s\n", plugin->getName().toRawUTF8(), name.toRawUTF8(),
                            seen.joinIntoString(", ").toRawUTF8());
            }
        }

        std::printf("Chain has %d block(s)\n", mProcessor->getChain().getNumBlocks());


        // One input channel and two outputs: the guitarist's real configuration,
        // and not the same test as feeding it an idealised stereo pair.
        mProcessor->setPlayConfigDetails(1, 2, sampleRate, blockSize);
        mProcessor->prepareToPlay(sampleRate, blockSize);

        std::printf("Processor negotiated %d in / %d out\n",
                    mProcessor->getTotalNumInputChannels(), mProcessor->getTotalNumOutputChannels());

        for (auto* block : mProcessor->getChain().getBlocks())
            if (auto* plugin = block->getPlugin())
                std::printf("  %-28s %d in / %d out  fed %s\n", block->getDisplayName().toRawUTF8(),
                            plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels(),
                            block->getSourceIsMono() ? "MONO" : "stereo");

        std::printf("\n");


        // Hard-decorrelated input: left only. Anything that sums to mono halves
        // it onto both sides, which is unmistakable in the numbers.
        const auto fill = [](juce::AudioBuffer<float>& buffer, bool leftOnly, int pass) {
            for (int channel = 0; channel < 2; ++channel)
            {
                auto* data = buffer.getWritePointer(channel);

                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    const auto phase = 0.07f * static_cast<float>(pass * buffer.getNumSamples() + i);
                    data[i] = (leftOnly && channel == 1) ? 0.0f : 0.25f * std::sin(phase);
                }
            }
        };

        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::MidiBuffer midi;

        for (const bool leftOnly : {true, false})
        {
            for (int pass = 0; pass < 60; ++pass)
            {
                fill(buffer, leftOnly, pass);
                midi.clear();
                mProcessor->processBlock(buffer, midi);
            }

            fill(buffer, leftOnly, 60);
            const auto inputDescription = describe(buffer);
            midi.clear();
            mProcessor->processBlock(buffer, midi);

            std::printf("%-22s in:  %s\n", leftOnly ? "left-only input" : "identical input",
                        inputDescription.toRawUTF8());
            std::printf("%-22s out: %s\n\n", "", describe(buffer).toRawUTF8());
        }

        // What a hosted plug-in actually sees when it asks for the tempo.
        mProcessor->getTransport().setBpm(93.5);

        {
            juce::AudioBuffer<float> tick(2, blockSize);
            juce::MidiBuffer tickMidi;
            tick.clear();
            mProcessor->processBlock(tick, tickMidi);
        }

        if (auto* block = mProcessor->getChain().getBlockByIndex(0))
            if (auto* plugin = block->getPlugin())
            {
                if (auto* playHead = plugin->getPlayHead())
                {
                    if (const auto position = playHead->getPosition())
                        std::printf("Plug-in playhead: bpm %.2f  ppq %.3f  playing %d\n",
                                    position->getBpm().orFallback(0.0),
                                    position->getPpqPosition().orFallback(-1.0),
                                    static_cast<int>(position->getIsPlaying()));
                    else
                        std::printf("Plug-in playhead: returns NO position\n");
                }
                else
                    std::printf("Plug-in playhead: NOT SET\n");
            }

        std::printf("Input mode is %s\n",
                    mProcessor->getInputMode() == BlockRigProcessor::InputMode::mono ? "MONO (sums to one channel"
                                                                                      " and copies it to both)"
                                                                                    : "stereo");

        mProcessor->releaseResources();
        juce::MessageManager::callAsync([] { juce::JUCEApplication::getInstance()->quit(); });
    }

    void runPluginCheck(const juce::String& fragment)
    {
        PluginCatalog catalog;
        catalog.setStorageDirectory(getStorageDirectory());
        catalog.loadFromStorage();

        const auto types = catalog.getKnownPluginList().getTypes();
        int checked = 0;

        std::printf("Searching %d catalogued plug-ins for \"%s\"\n\n", types.size(),
                    fragment.toRawUTF8());

        for (const auto& description : types)
        {
            if (fragment.isNotEmpty() && !description.name.containsIgnoreCase(fragment))
                continue;

            if (++checked > 6)
                break;

            juce::String error;
            auto instance = catalog.getFormatManager().createPluginInstance(description, 48000.0, 512, error);

            if (instance == nullptr)
            {
                std::printf("%-34s could not load: %s\n", description.name.toRawUTF8(),
                            error.toRawUTF8());
                continue;
            }

            BlockInstance block(std::move(instance), "check");
            block.prepare(48000.0, 512);

            auto* plugin = block.getPlugin();

            // Identical content on both sides, which is what a mono guitar
            // becomes in the lane. A stereo plug-in should decorrelate it.
            juce::AudioBuffer<float> buffer(2, 512);
            juce::MidiBuffer midi;

            for (int pass = 0; pass < 40; ++pass)
            {
                for (int channel = 0; channel < 2; ++channel)
                {
                    auto* data = buffer.getWritePointer(channel);
                    for (int i = 0; i < 512; ++i)
                        data[i] = 0.25f * std::sin(0.07f * static_cast<float>(pass * 512 + i));
                }

                midi.clear();
                block.process(buffer, midi, 512.0 / 48000.0);
            }

            double difference = 0.0;
            for (int i = 0; i < 512; ++i)
                difference += std::abs(buffer.getReadPointer(0)[i] - buffer.getReadPointer(1)[i]);
            difference /= 512.0;

            std::printf("%-34s %d in / %d out%s   L-R difference %.5f  %s\n",
                        description.name.toRawUTF8(), plugin->getTotalNumInputChannels(),
                        plugin->getTotalNumOutputChannels(),
                        block.isMonoOnly() ? "  MONO-ONLY" : "          ", difference,
                        difference > 1.0e-4 ? "STEREO" : "mono/identical");
        }

        if (checked == 0)
            std::printf("Nothing matched. Has the catalogue been scanned?\n");

        juce::MessageManager::callAsync([] { juce::JUCEApplication::getInstance()->quit(); });
    }

    /// Prints the real audio configuration and measures the input for a few
    /// seconds. Diagnostic, but also the fastest way for a user to answer "is my
    /// guitar reaching this app at all".
    void runAudioCheck()
    {
        mProcessor = std::make_unique<BlockRigProcessor>();
        mProcessor->setMuted(true); // measuring input only; do not make noise
        setUpAudio();

        auto* device = mDeviceManager.getCurrentAudioDevice();

        if (device == nullptr)
        {
            std::printf("No audio device could be opened.\n");
            juce::MessageManager::callAsync([] { juce::JUCEApplication::getInstance()->quit(); });
            return;
        }

        const auto setup = mDeviceManager.getAudioDeviceSetup();

        std::printf("Device type : %s\n", mDeviceManager.getCurrentAudioDeviceType().toRawUTF8());
        std::printf("Device      : %s\n", device->getName().toRawUTF8());
        std::printf("Sample rate : %.0f Hz, buffer %d\n", device->getCurrentSampleRate(),
                    device->getCurrentBufferSizeSamples());

        const auto inputNames = device->getInputChannelNames();
        const auto activeIn = device->getActiveInputChannels();
        const auto activeOut = device->getActiveOutputChannels();

        std::printf("Inputs      : %d available, %d active\n", inputNames.size(),
                    activeIn.countNumberOfSetBits());

        for (int i = 0; i < inputNames.size(); ++i)
            if (activeIn[i])
                std::printf("              [%d] %s  ACTIVE\n", i + 1, inputNames[i].toRawUTF8());

        std::printf("Outputs     : %d active\n", activeOut.countNumberOfSetBits());
        std::printf("Processor   : %d in / %d out\n", mProcessor->getTotalNumInputChannels(),
                    mProcessor->getTotalNumOutputChannels());
        std::printf("Muted       : %s\n", mProcessor->isMuted() ? "yes" : "no");
        std::printf("Chain blocks: %d\n", mProcessor->getChain().getNumBlocks());
        std::printf("\nPhase 1 (muted) — play something for 4 seconds...\n");
        std::fflush(stdout);

        mCheckThread = std::thread([this] {
            const auto measure = [this](int tenths) {
                float loudestIn = 0.0f, loudestOut = 0.0f;

                for (int i = 0; i < tenths; ++i)
                {
                    juce::Thread::sleep(100);
                    loudestIn = juce::jmax(loudestIn, mProcessor->getInputLevel());
                    loudestOut = juce::jmax(loudestOut, mProcessor->getOutputLevel());
                }

                return std::make_pair(loudestIn, loudestOut);
            };

            const auto describe = [](float level) {
                return level > 0.0f ? juce::String(juce::Decibels::gainToDecibels(level), 1) + " dBFS"
                                    : juce::String("silent");
            };

            const auto [mutedIn, mutedOut] = measure(40);
            std::printf("  input  %s\n  output %s   (expected silent while muted)\n",
                        describe(mutedIn).toRawUTF8(), describe(mutedOut).toRawUTF8());

            std::printf("\nPhase 2 (un-muted) — keep playing for 4 seconds...\n");
            std::fflush(stdout);
            juce::MessageManager::callAsync([this] { mProcessor->setMuted(false); });

            const auto [liveIn, liveOut] = measure(40);
            std::printf("  input  %s\n  output %s\n", describe(liveIn).toRawUTF8(),
                        describe(liveOut).toRawUTF8());

            std::printf("\n--- verdict ---\n");

            if (liveIn <= 0.0005f)
                std::printf("NO INPUT: nothing is reaching the app's input channel.\n");
            else if (liveOut <= 0.0005f)
                std::printf("INPUT OK, NO OUTPUT: signal arrives but does not leave the processor.\n");
            else
                std::printf("INPUT AND OUTPUT BOTH WORKING (in %s, out %s).\n",
                            describe(liveIn).toRawUTF8(), describe(liveOut).toRawUTF8());

            std::fflush(stdout);
            juce::MessageManager::callAsync([] { juce::JUCEApplication::getInstance()->quit(); });
        });
    }

    /// A multi-channel interface can come up with every input channel disabled,
    /// which looks exactly like a broken app: signal reaches the interface but
    /// never reaches us. If nothing is enabled, turn the first input on.
    void ensureAnInputIsEnabled()
    {
        auto* device = mDeviceManager.getCurrentAudioDevice();
        if (device == nullptr)
            return;

        auto setup = mDeviceManager.getAudioDeviceSetup();

        if (!setup.inputChannels.isZero())
            return;

        const int available = device->getInputChannelNames().size();
        if (available <= 0)
            return;

        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setBit(0);
        if (available > 1)
            setup.inputChannels.setBit(1);

        const auto error = mDeviceManager.setAudioDeviceSetup(setup, true);

        if (error.isNotEmpty())
            juce::Logger::writeToLog("Could not enable an input channel: " + error);
    }

    void saveDeviceState()
    {
        juce::XmlElement settings("BlockRigSettings");

        if (auto deviceState = mDeviceManager.createStateXml())
        {
            auto* wrapper = settings.createNewChildElement(kDeviceStateKey);
            wrapper->addChildElement(deviceState.release());
        }

        getStorageDirectory().createDirectory();
        settings.writeTo(getSettingsFile());
    }

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow(const juce::String& name, BlockRigProcessor& processor, juce::AudioDeviceManager& deviceManager)
            : juce::DocumentWindow(name, juce::Colour(0xff101216), juce::DocumentWindow::allButtons)
        {
            // This is a stage instrument: everything wants to be readable at
            // arm's length on a laptop, not at desk-and-mouse density. Scaling
            // the desktop rather than every token keeps the design's own
            // proportions intact, and shrinks on small screens rather than
            // opening a window that will not fit.
            {
                const auto work = juce::Desktop::getInstance()
                                      .getDisplays()
                                      .getPrimaryDisplay()
                                      ->userArea;
                const auto fits = juce::jmin(static_cast<float>(work.getWidth()) / 1500.0f,
                                             static_cast<float>(work.getHeight()) / 940.0f);
                juce::Desktop::getInstance().setGlobalScaleFactor(juce::jlimit(1.0f, 1.3f, fits));
            }

            setUsingNativeTitleBar(true);
            setContentOwned(new AppShell(processor, &deviceManager), true);
            setResizable(true, false);
            setResizeLimits(1000, 600, 4200, 2600);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        AppShell& getShell() { return *static_cast<AppShell*>(getContentComponent()); }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<DownloadsWatcher> mDownloadsWatcher;
    std::unique_ptr<PluginScannerWorker> mScannerWorker;
    std::unique_ptr<PluginCatalog> mCatalogForScan;
    std::thread mScanThread;
    std::thread mCheckThread;
    std::unique_ptr<BlockRigProcessor> mProcessor;
    juce::AudioDeviceManager mDeviceManager;
    juce::AudioProcessorPlayer mPlayer;
    std::unique_ptr<MainWindow> mMainWindow;
    std::unique_ptr<AutoSave> mAutoSave;
};

} // namespace blockrig

START_JUCE_APPLICATION(blockrig::StandaloneApp)
