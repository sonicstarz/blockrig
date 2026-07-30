#include "ui/AppShell.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include "state/RigFiles.h"
#include "ui/Theme.h"

namespace blockrig
{
namespace
{
void drawWordmark(juce::Graphics& g, juce::Rectangle<float> area, float scale)
{
    g.setColour(theme::colours::text);
    g.setFont(juce::FontOptions(34.0f * scale, juce::Font::bold));
    g.drawText("BLOCK", area.removeFromLeft(area.getWidth() * 0.5f), juce::Justification::centredRight,
               false);
    g.setColour(theme::colours::accent);
    g.drawText("RIG", area, juce::Justification::centredLeft, false);
}
} // namespace

//==============================================================================
/// Rigs, chosen and managed. Audio settings live here too, because "set up my
/// interface" is the first thing a new install needs and the last thing that
/// should hide behind an icon.
class AppShell::HomeScreen final : public juce::Component
{
public:
    HomeScreen(AppShell& shell, BlockRigProcessor& processor, juce::AudioDeviceManager* deviceManager)
        : mShell(shell)
        , mProcessor(processor)
        , mDeviceManager(deviceManager)
    {
        mNewRig.onClick = [this] { createRig(); };
        addAndMakeVisible(mNewRig);

        mAudioSettings.setEnabled(deviceManager != nullptr);
        mAudioSettings.onClick = [this] { showAudioSettings(); };
        addAndMakeVisible(mAudioSettings);

        mRescan.onClick = [this] {
            // The boot sweep only picks up new files; this is the full rescan.
            mShell.beginBoot();
        };
        addAndMakeVisible(mRescan);

        mShowFolder.onClick = [] { MainView::getRigsFolder().revealToUser(); };
        addAndMakeVisible(mShowFolder);

        mList.setRowHeight(52);
        mList.setModel(&mModel);
        mList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(mList);

        refresh();
    }

    void refresh()
    {
        mRigs = MainView::getRigsFolder().findChildFiles(juce::File::findFiles, false,
                                                         "*" + juce::String(rigfiles::kFileExtension));

        // Most recently touched first: the rig from last night is the one wanted.
        std::sort(mRigs.begin(), mRigs.end(), [](const juce::File& a, const juce::File& b) {
            return a.getLastModificationTime() > b.getLastModificationTime();
        });

        mList.updateContent();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();

        drawWordmark(g, area.removeFromTop(120.0f).reduced(0.0f, 34.0f), 1.0f);

        g.setColour(theme::colours::textFaint);
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText("RIGS",
                   getLocalBounds().withTrimmedTop(158).removeFromTop(16).reduced(
                       juce::jmax(theme::metrics::padding, getWidth() / 2 - 330), 0),
                   juce::Justification::centredLeft, false);

        if (mRigs.isEmpty())
        {
            g.setColour(theme::colours::textFaint);
            g.setFont(juce::FontOptions(14.0f));
            g.drawText("No rigs yet — create one to get started",
                       getLocalBounds().withTrimmedTop(200), juce::Justification::centredTop, false);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop(118);

        auto buttons = area.removeFromTop(36).withSizeKeepingCentre(juce::jmin(area.getWidth() - 40, 660), 32);
        const int buttonWidth = buttons.getWidth() / 4;
        mNewRig.setBounds(buttons.removeFromLeft(buttonWidth).reduced(4, 0));
        mAudioSettings.setBounds(buttons.removeFromLeft(buttonWidth).reduced(4, 0));
        mRescan.setBounds(buttons.removeFromLeft(buttonWidth).reduced(4, 0));
        mShowFolder.setBounds(buttons.reduced(4, 0));

        area.removeFromTop(26);
        mList.setBounds(area.withSizeKeepingCentre(juce::jmin(area.getWidth() - 40, 660),
                                                   area.getHeight() - 16));
    }

private:
    void createRig()
    {
        auto window = std::make_shared<juce::AlertWindow>("New rig", "", juce::MessageBoxIconType::NoIcon,
                                                          this);
        window->addTextEditor("name", "My rig");
        window->addButton("Create", 1, juce::KeyPress(juce::KeyPress::returnKey));
        window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        window->enterModalState(true, juce::ModalCallbackFunction::create([this, window](int result) {
            if (result != 1)
                return;

            auto name = juce::File::createLegalFileName(window->getTextEditorContents("name").trim());
            if (name.isEmpty())
                name = "My rig";

            auto file = MainView::getRigsFolder()
                            .getChildFile(name + juce::String(rigfiles::kFileExtension))
                            .getNonexistentSibling();

            // An empty rig: clear whatever the processor is holding, save, open.
            mProcessor.notifyBlockRemoval({});
            mProcessor.getChain().clear();
            mProcessor.getSnapshots().getSnapshots().clear();
            mProcessor.getSnapshots().activeIndex = -1;

            juce::String error;
            rigfiles::save(mProcessor, file, error);

            mShell.openRig(file);
        }));
    }

    void showAudioSettings()
    {
        if (mDeviceManager == nullptr)
            return;

        auto* selector = new juce::AudioDeviceSelectorComponent(*mDeviceManager, 1, 2, 0, 2,
                                                                false, false, false, false);
        selector->setSize(520, 420);

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(selector);
        options.dialogTitle = "Audio settings";
        options.dialogBackgroundColour = theme::colours::background;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }

    /// One row per rig: name and date, with rename and delete on the right.
    class Model final : public juce::ListBoxModel
    {
    public:
        explicit Model(HomeScreen& owner)
            : mOwner(owner)
        {
        }

        int getNumRows() override { return mOwner.mRigs.size(); }

        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool hovered) override
        {
            if (row >= mOwner.mRigs.size())
                return;

            const auto& file = mOwner.mRigs.getReference(row);
            auto area = juce::Rectangle<int>(0, 0, width, height).reduced(2, 3).toFloat();

            g.setColour(hovered ? theme::colours::panelRaised : theme::colours::panel);
            g.fillRoundedRectangle(area, theme::metrics::smallCornerRadius);
            g.setColour(theme::colours::outline);
            g.drawRoundedRectangle(area, theme::metrics::smallCornerRadius, 1.0f);

            g.setColour(theme::colours::text);
            g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
            g.drawText(file.getFileNameWithoutExtension(), area.reduced(14.0f, 0.0f).removeFromTop(30.0f),
                       juce::Justification::bottomLeft, true);

            g.setColour(theme::colours::textFaint);
            g.setFont(juce::FontOptions(11.0f));
            g.drawText(file.getLastModificationTime().toString(true, true, false),
                       area.reduced(14.0f, 4.0f), juce::Justification::bottomLeft, false);
        }

        void listBoxItemClicked(int row, const juce::MouseEvent& event) override
        {
            if (row >= mOwner.mRigs.size())
                return;

            const auto file = mOwner.mRigs[row];

            if (event.mods.isPopupMenu())
            {
                juce::PopupMenu menu;
                menu.addItem(1, "Open");
                menu.addItem(2, "Rename...");
                menu.addItem(3, "Delete...");

                menu.showMenuAsync(juce::PopupMenu::Options(), [this, file](int choice) {
                    if (choice == 1)
                        mOwner.mShell.openRig(file);
                    else if (choice == 2)
                        mOwner.renameRig(file);
                    else if (choice == 3)
                        mOwner.deleteRig(file);
                });
                return;
            }

            mOwner.mShell.openRig(file);
        }

    private:
        HomeScreen& mOwner;
    };

    void renameRig(const juce::File& file)
    {
        auto window = std::make_shared<juce::AlertWindow>("Rename rig", "", juce::MessageBoxIconType::NoIcon,
                                                          this);
        window->addTextEditor("name", file.getFileNameWithoutExtension());
        window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
        window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        window->enterModalState(true, juce::ModalCallbackFunction::create([this, file, window](int result) {
            const auto text = juce::File::createLegalFileName(window->getTextEditorContents("name").trim());

            if (result == 1 && text.isNotEmpty())
            {
                const auto target =
                    file.getSiblingFile(text + juce::String(rigfiles::kFileExtension));

                if (!target.existsAsFile())
                    file.moveFileTo(target);
            }

            refresh();
        }));
    }

    void deleteRig(const juce::File& file)
    {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Delete \"" + file.getFileNameWithoutExtension() + "\"?")
                .withMessage("The rig file moves to the Trash.")
                .withButton("Delete")
                .withButton("Cancel")
                .withAssociatedComponent(this),
            [this, file](int result) {
                if (result == 1)
                    file.moveToTrash();
                refresh();
            });
    }

    AppShell& mShell;
    BlockRigProcessor& mProcessor;
    juce::AudioDeviceManager* mDeviceManager;

    juce::Array<juce::File> mRigs;
    juce::TextButton mNewRig{"New rig"};
    juce::TextButton mAudioSettings{"Audio settings"};
    juce::TextButton mRescan{"Rescan plug-ins"};
    juce::TextButton mShowFolder{"Show rigs folder"};
    Model mModel{*this};
    juce::ListBox mList;
};

//==============================================================================
AppShell::AppShell(BlockRigProcessor& processor, juce::AudioDeviceManager* deviceManager)
    : mProcessor(processor)
    , mDeviceManager(deviceManager)
{
    setOpaque(true);
    setSize(1440, 820);
}

AppShell::~AppShell()
{
    if (mScanThread.joinable())
        mScanThread.join();
}

void AppShell::beginBoot()
{
    if (mScanThread.joinable())
        mScanThread.join();

    mScreen = Screen::boot;
    mHome = nullptr;
    mMainView = nullptr;
    mScanDone.store(false);
    mBootElapsedTicks = 0;

    {
        const juce::ScopedLock lock(mProgressLock);
        mProgressText = "Checking plug-ins…";
    }

    mScanThread = std::thread([this] { scanThreadBody(); });
    startTimerHz(10);
    repaint();
}

void AppShell::scanThreadBody()
{
    // Prune plug-ins whose files are gone, then sweep for anything new. The
    // scanner skips files already in the list, so an unchanged machine gets
    // through this in seconds.
    auto& catalog = mProcessor.getCatalog();
    auto& list = catalog.getKnownPluginList();

    // Only entries that are real file paths can be pruned; AudioUnits identify
    // themselves as "AudioUnit:Effects/..." and must be left alone.
    for (const auto& type : list.getTypes())
        if (juce::File::isAbsolutePath(type.fileOrIdentifier)
            && !juce::File(type.fileOrIdentifier).exists())
            list.removeType(type);

    catalog.scanAllFormats([this](const PluginCatalog::ScanProgress& progress) {
        const juce::ScopedLock lock(mProgressLock);
        mProgressText = progress.total > 0
                            ? "Scanning " + juce::String(progress.scanned) + " / "
                                  + juce::String(progress.total) + "   " + progress.currentPluginName
                            : juce::String("Checking plug-ins…");
    });

    catalog.saveToStorage();
    mScanDone.store(true);
}

void AppShell::timerCallback()
{
    ++mBootElapsedTicks;

    if (mScreen == Screen::boot)
    {
        // Hold the logo a moment even when the sweep is instant; a boot screen
        // that flickers for two frames reads as a glitch.
        if (mScanDone.load() && mBootElapsedTicks >= 12)
        {
            stopTimer();

            if (mScanThread.joinable())
                mScanThread.join();

            showHome();
            return;
        }

        repaint();
    }
}

void AppShell::showHome()
{
    mScreen = Screen::home;
    mMainView = nullptr;

    mHome = std::make_unique<HomeScreen>(*this, mProcessor, mDeviceManager);
    addAndMakeVisible(*mHome);
    resized();
    repaint();
}

void AppShell::openRig(const juce::File& file)
{
    mScreen = Screen::rig;
    mHome = nullptr;

    mMainView = std::make_unique<MainView>(mProcessor, mDeviceManager);
    addAndMakeVisible(*mMainView);

    // The logo in the rig header is the way back here.
    mMainView->onHomeRequested = [this] {
        if (mMainView != nullptr)
            mMainView->confirmThenSwitch([this] { showHome(); });
    };

    if (file.existsAsFile())
        mMainView->loadRigFile(file);

    resized();
}

void AppShell::requestQuit(std::function<void()> quitNow)
{
    if (mMainView != nullptr)
        mMainView->confirmThenSwitch(std::move(quitNow));
    else
        quitNow();
}

void AppShell::paint(juce::Graphics& g)
{
    g.setGradientFill(juce::ColourGradient(theme::colours::background.brighter(0.05f), 0.0f, 0.0f,
                                           theme::colours::background.darker(0.15f), 0.0f,
                                           static_cast<float>(getHeight()), false));
    g.fillAll();

    if (mScreen != Screen::boot)
        return;

    auto area = getLocalBounds().toFloat();
    auto logoArea = area.withSizeKeepingCentre(area.getWidth(), 60.0f).translated(0.0f, -20.0f);
    drawWordmark(g, logoArea, 1.4f);

    juce::String progress;
    {
        const juce::ScopedLock lock(mProgressLock);
        progress = mProgressText;
    }

    g.setColour(theme::colours::textFaint);
    g.setFont(juce::FontOptions(12.5f));
    g.drawText(progress, getLocalBounds().withTrimmedTop(getHeight() / 2 + 40).removeFromTop(20),
               juce::Justification::centred, false);

    // A slow sweep under the wordmark, so a long first scan visibly lives.
    const auto sweep = getLocalBounds().toFloat().withSizeKeepingCentre(260.0f, 3.0f).translated(0.0f, 34.0f);
    g.setColour(theme::colours::outline);
    g.fillRoundedRectangle(sweep, 1.5f);
    const auto phase = static_cast<float>(mBootElapsedTicks % 30) / 30.0f;
    g.setColour(theme::colours::accent);
    g.fillRoundedRectangle(sweep.withWidth(70.0f).translated(phase * 190.0f, 0.0f), 1.5f);
}

void AppShell::resized()
{
    if (mHome != nullptr)
        mHome->setBounds(getLocalBounds());
    if (mMainView != nullptr)
        mMainView->setBounds(getLocalBounds());
}

} // namespace blockrig
