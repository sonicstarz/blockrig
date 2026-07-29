#include "host/PluginCatalog.h"

#include <condition_variable>
#include <mutex>

#include "host/InternalBlockFormat.h"
#include "host/PluginScannerWorker.h"

namespace blockrig
{
namespace
{
constexpr const char* kPluginListFileName = "PluginList.xml";
constexpr const char* kDeadMansPedalFileName = "ScannerDeadMansPedal";
} // namespace

//==============================================================================
/// Parent-side handle on one scanner child process.
class PluginCatalog::Superprocess final : private juce::ChildProcessCoordinator
{
public:
    Superprocess()
    {
        launchWorkerProcess(juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                            PluginScannerWorker::kProcessUid, 0, 0);
    }

    enum class State
    {
        timeout,
        gotResult,
        connectionLost
    };

    struct Response
    {
        State state;
        std::unique_ptr<juce::XmlElement> xml;
    };

    Response getResponse()
    {
        std::unique_lock<std::mutex> lock{mMutex};

        if (!mCondition.wait_for(lock, std::chrono::milliseconds{50},
                                 [this] { return mGotResult || mConnectionLost; }))
            return {State::timeout, nullptr};

        const auto state = mConnectionLost ? State::connectionLost : State::gotResult;
        mConnectionLost = false;
        mGotResult = false;

        return {state, std::move(mDescription)};
    }

    using juce::ChildProcessCoordinator::sendMessageToWorker;

private:
    void handleMessageFromWorker(const juce::MemoryBlock& block) override
    {
        const std::lock_guard<std::mutex> lock{mMutex};
        mDescription = juce::parseXML(block.toString());
        mGotResult = true;
        mCondition.notify_one();
    }

    void handleConnectionLost() override
    {
        const std::lock_guard<std::mutex> lock{mMutex};
        mConnectionLost = true;
        mCondition.notify_one();
    }

    std::mutex mMutex;
    std::condition_variable mCondition;
    std::unique_ptr<juce::XmlElement> mDescription;
    bool mConnectionLost = false;
    bool mGotResult = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Superprocess)
};

//==============================================================================
/// Routes every probe that KnownPluginList wants through a child process, and
/// enforces the per-plug-in timeout.
class PluginCatalog::Scanner final : public juce::KnownPluginList::CustomScanner
{
public:
    Scanner(PluginCatalog& owner)
        : mOwner(owner)
    {
    }

    bool findPluginTypesFor(juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                            const juce::String& fileOrIdentifier) override
    {
        if (mOwner.mScanInProcess)
        {
            mSuperprocess = nullptr;
            format.findAllTypesForFile(result, fileOrIdentifier);
            return true;
        }

        if (probeInChildProcess(format.getName(), fileOrIdentifier, result))
            return true;

        // The child is unrecoverable: drop it so the next plug-in gets a fresh one.
        mSuperprocess = nullptr;
        return false;
    }

    void scanFinished() override { mSuperprocess = nullptr; }

    int getTimeoutCount() const { return mTimeouts; }
    const juce::StringArray& getTimedOutPlugins() const { return mTimedOut; }
    void resetCounts()
    {
        mTimeouts = 0;
        mTimedOut.clear();
    }

private:
    bool probeInChildProcess(const juce::String& formatName, const juce::String& fileOrIdentifier,
                             juce::OwnedArray<juce::PluginDescription>& result)
    {
        if (mSuperprocess == nullptr)
            mSuperprocess = std::make_unique<Superprocess>();

        juce::MemoryBlock block;
        juce::MemoryOutputStream stream{block, true};
        stream.writeString(formatName);
        stream.writeString(fileOrIdentifier);
        // Tell the child our deadline so its own watchdog can align with ours.
        stream.writeInt(static_cast<int>(mOwner.mPerPluginTimeout.inMilliseconds()));
        stream.flush();

        if (!mSuperprocess->sendMessageToWorker(block))
            return false;

        // The watchdog. JUCE's example loops on timeout forever, so one hung
        // plug-in stalls the whole scan; we give each plug-in a deadline and
        // then kill its child.
        const auto deadline = juce::Time::getCurrentTime() + mOwner.mPerPluginTimeout;

        for (;;)
        {
            if (shouldExit())
                return true;

            const auto response = mSuperprocess->getResponse();

            if (response.state == Superprocess::State::timeout)
            {
                if (juce::Time::getCurrentTime() < deadline)
                    continue;

                ++mTimeouts;
                mTimedOut.add(fileOrIdentifier);

                // Killing the child is what unblocks us; the dead man's pedal
                // already names this plug-in, so it gets denylisted.
                mSuperprocess = nullptr;
                return false;
            }

            if (response.xml != nullptr)
            {
                for (const auto* item : response.xml->getChildIterator())
                {
                    auto description = std::make_unique<juce::PluginDescription>();
                    if (description->loadFromXml(*item))
                        result.add(std::move(description));
                }
            }

            return response.state == Superprocess::State::gotResult;
        }
    }

    PluginCatalog& mOwner;
    std::unique_ptr<Superprocess> mSuperprocess;
    int mTimeouts = 0;
    juce::StringArray mTimedOut;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Scanner)
};

//==============================================================================
PluginCatalog::PluginCatalog()
{
    juce::addDefaultFormatsToManager(mFormats);

    // Built-in blocks come from our own format, so the picker and the chain treat
    // them exactly like scanned plug-ins.
    auto internalFormat = std::make_unique<InternalBlockFormat>();
    for (const auto& description : internalFormat->getAllTypes())
        mBuiltIns.add(description);
    mFormats.addFormat(std::move(internalFormat));

    auto scanner = std::make_unique<Scanner>(*this);
    mScanner = scanner.get();
    mKnownPlugins.setCustomScanner(std::move(scanner));
}

PluginCatalog::~PluginCatalog() = default;

void PluginCatalog::setStorageDirectory(const juce::File& directory)
{
    mStorageDirectory = directory;
    if (mStorageDirectory != juce::File{})
        mStorageDirectory.createDirectory();
}

juce::File PluginCatalog::getPluginListFile() const
{
    return mStorageDirectory.getChildFile(kPluginListFileName);
}

juce::File PluginCatalog::getDeadMansPedalFile() const
{
    return mStorageDirectory.getChildFile(kDeadMansPedalFileName);
}

juce::Array<juce::PluginDescription> PluginCatalog::getBuiltInDescriptions() const
{
    return mBuiltIns;
}

void PluginCatalog::loadFromStorage()
{
    if (const auto xml = juce::parseXML(getPluginListFile()))
        mKnownPlugins.recreateFromXml(*xml);

    // If a previous run died while probing something, that plug-in is named in
    // the pedal file; denylist it now rather than crashing the same way again.
    if (getDeadMansPedalFile().existsAsFile())
        juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal(mKnownPlugins, getDeadMansPedalFile());
}

void PluginCatalog::saveToStorage() const
{
    if (mStorageDirectory == juce::File{})
        return;

    if (const auto xml = mKnownPlugins.createXml())
        xml->writeTo(getPluginListFile());
}

void PluginCatalog::setScanInProcess(bool shouldScanInProcess)
{
    mScanInProcess = shouldScanInProcess;
}

void PluginCatalog::clearDenylist()
{
    // getBlacklistedFiles() returns a reference to the live array, so removing
    // while iterating it skips entries. Copy first.
    const auto denylisted = mKnownPlugins.getBlacklistedFiles();

    for (const auto& file : denylisted)
        mKnownPlugins.removeFromBlacklist(file);

    saveToStorage();
}

PluginCatalog::ScanSummary PluginCatalog::scanAllFormats(std::function<void(const ScanProgress&)> onProgress,
                                                         std::function<bool()> shouldAbort)
{
    ScanSummary summary;

    const auto denylistBefore = mKnownPlugins.getBlacklistedFiles();
    const int foundBefore = mKnownPlugins.getNumTypes();

    for (auto* format : mFormats.getFormats())
    {
        if (!format->canScanForPlugins())
            continue; // built-ins and anything else that needs no probing

        juce::PluginDirectoryScanner scanner(mKnownPlugins, *format, format->getDefaultLocationsToSearch(),
                                             true, getDeadMansPedalFile(), true);

        // The scanner does its own enumeration internally and does not expose the
        // count, so enumerate separately just to show an accurate total.
        const int total =
            format->searchPathsForPlugins(format->getDefaultLocationsToSearch(), true, true).size();
        int scanned = 0;

        for (;;)
        {
            if (shouldAbort && shouldAbort())
                break;

            juce::String nextName = scanner.getNextPluginFileThatWillBeScanned();
            const bool more = scanner.scanNextFile(true, nextName);

            ++scanned;
            ++summary.scanned;

            if (onProgress)
                onProgress(ScanProgress{nextName, scanned, total, mKnownPlugins.getNumTypes() - foundBefore});

            // Persist as we go: a later crash then costs one plug-in, not the run.
            if (scanned % 8 == 0)
                saveToStorage();

            if (!more)
                break;
        }

        // Files that looked like plug-ins but would not open: worth surfacing,
        // and distinct from an outright crash.
        for (const auto& failed : scanner.getFailedFiles())
            if (!summary.failedNames.contains(failed))
                summary.failedNames.add(failed);

        saveToStorage();
    }

    summary.found = mKnownPlugins.getNumTypes() - foundBefore;

    for (const auto& file : mKnownPlugins.getBlacklistedFiles())
        if (!denylistBefore.contains(file))
            summary.denylistedNames.add(file);

    summary.denylisted = summary.denylistedNames.size();

    if (mScanner != nullptr)
    {
        summary.timedOut = mScanner->getTimeoutCount();
        for (const auto& name : mScanner->getTimedOutPlugins())
            if (!summary.denylistedNames.contains(name))
                summary.denylistedNames.add(name);
        mScanner->resetCounts();
    }

    saveToStorage();
    return summary;
}

} // namespace blockrig
