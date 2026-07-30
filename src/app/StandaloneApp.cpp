#include <thread>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "BlockRigProcessor.h"
#include "host/PluginCatalog.h"
#include "host/PluginScannerWorker.h"
#include "ui/MainView.h"

namespace blockrig
{
namespace
{
constexpr const char* kSettingsFileName = "BlockRig.settings";
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

        mProcessor = std::make_unique<BlockRigProcessor>();
        mProcessor->getCatalog().setStorageDirectory(getStorageDirectory());
        mProcessor->getCatalog().loadFromStorage();

        // The app opens a live input into a live output, so start silent and let
        // the user commit. (wrapperType cannot tell us we are the app: this
        // processor is built directly, not through a plug-in wrapper.)
        mProcessor->setMuted(true);

        setUpAudio();

        mMainWindow = std::make_unique<MainWindow>(getApplicationName(), *mProcessor, mDeviceManager);
    }

    void shutdown() override
    {
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
            mPlayer.setProcessor(nullptr);
            mDeviceManager.removeAudioCallback(&mPlayer);
        }

        mStatusLogger = nullptr;
        mProcessor = nullptr;
        mScannerWorker = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    /// Somewhere both the app and a DAW-sandboxed plug-in instance can read.
    static juce::File getStorageDirectory()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Application Support")
            .getChildFile("BlockRig");
    }

    static juce::File getSettingsFile() { return getStorageDirectory().getChildFile(kSettingsFileName); }

    void setUpAudio()
    {
        std::unique_ptr<juce::XmlElement> savedState;

        if (const auto settings = juce::parseXML(getSettingsFile()))
            if (auto* wrapper = settings->getChildByName(kDeviceStateKey))
                if (auto* deviceState = wrapper->getFirstChildElement())
                    savedState = std::make_unique<juce::XmlElement>(*deviceState);

        // Guitar in, stereo out: one input channel is the common case, so do not
        // demand two.
        mDeviceManager.initialise(2, 2, savedState.get(), true);

        ensureAnInputIsEnabled();

        mPlayer.setProcessor(mProcessor.get());
        mDeviceManager.addAudioCallback(&mPlayer);

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
        mStatusLogger = std::make_unique<StatusLogger>(*mProcessor, logFile);
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
        StatusLogger(BlockRigProcessor& processor, juce::File file)
            : mProcessor(processor)
            , mFile(std::move(file))
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

            mFile.appendText("levels: in " + juce::String(mPeakIn, 5) + "  out "
                             + juce::String(mPeakOut, 5) + "  muted "
                             + juce::String(mProcessor.isMuted() ? "yes" : "no") + "\n");
            mPeakIn = 0.0f;
            mPeakOut = 0.0f;
        }

        BlockRigProcessor& mProcessor;
        juce::File mFile;
        float mPeakIn = 0.0f, mPeakOut = 0.0f;
        int mTicks = 0;
    };

    std::unique_ptr<StatusLogger> mStatusLogger;

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
            setUsingNativeTitleBar(true);
            setContentOwned(new MainView(processor, &deviceManager), true);
            setResizable(true, false);
            setResizeLimits(900, 520, 4000, 2400);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<PluginScannerWorker> mScannerWorker;
    std::unique_ptr<PluginCatalog> mCatalogForScan;
    std::thread mScanThread;
    std::thread mCheckThread;
    std::unique_ptr<BlockRigProcessor> mProcessor;
    juce::AudioDeviceManager mDeviceManager;
    juce::AudioProcessorPlayer mPlayer;
    std::unique_ptr<MainWindow> mMainWindow;
};

} // namespace blockrig

START_JUCE_APPLICATION(blockrig::StandaloneApp)
