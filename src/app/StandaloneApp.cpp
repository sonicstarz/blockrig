#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "BlockRigProcessor.h"
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
    bool moreThanOneInstanceAllowed() override { return false; }

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

        mProcessor = std::make_unique<BlockRigProcessor>();
        mProcessor->getCatalog().setStorageDirectory(getStorageDirectory());
        mProcessor->getCatalog().loadFromStorage();

        setUpAudio();

        mMainWindow = std::make_unique<MainWindow>(getApplicationName(), *mProcessor, mDeviceManager);
    }

    void shutdown() override
    {
        mMainWindow = nullptr;

        if (mProcessor != nullptr)
        {
            saveDeviceState();
            mProcessor->getCatalog().saveToStorage();
            mPlayer.setProcessor(nullptr);
            mDeviceManager.removeAudioCallback(&mPlayer);
        }

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

        mPlayer.setProcessor(mProcessor.get());
        mDeviceManager.addAudioCallback(&mPlayer);
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
    std::unique_ptr<BlockRigProcessor> mProcessor;
    juce::AudioDeviceManager mDeviceManager;
    juce::AudioProcessorPlayer mPlayer;
    std::unique_ptr<MainWindow> mMainWindow;
};

} // namespace blockrig

START_JUCE_APPLICATION(blockrig::StandaloneApp)
