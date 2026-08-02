#include "ui/AppShell.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include "state/RigFiles.h"
#include "state/RigState.h"
#include "state/Setlist.h"
#include "ui/Theme.h"
#include "ui/Tone3000Panel.h"

namespace blockrig
{
namespace
{
/// The design's easing — cubic-bezier(0.2, 0.8, 0.2, 1) — close enough to an
/// ease-out cubic for flight paths.
float easeOut(float t)
{
    const auto inverted = 1.0f - juce::jlimit(0.0f, 1.0f, t);
    return 1.0f - inverted * inverted * inverted;
}
} // namespace

//==============================================================================
/// tvOS-style shelves: Rigs, Setlists, Tools. One component paints everything;
/// hover is focus (scale + white ring, the rest at 85%), click opens.
class AppShell::HomeScreen final : public juce::Component
{
public:
    HomeScreen(AppShell& shell, BlockRigProcessor& processor, juce::AudioDeviceManager* deviceManager)
        : mShell(shell)
        , mProcessor(processor)
        , mDeviceManager(deviceManager)
    {
        refresh();
    }

    void refresh()
    {
        mTiles.clear();

        // --- Rigs shelf, most recently played first.
        auto rigs = MainView::getRigsFolder().findChildFiles(juce::File::findFiles, false,
                                                            "*" + juce::String(rigfiles::kFileExtension));
        std::sort(rigs.begin(), rigs.end(), [](const juce::File& a, const juce::File& b) {
            return a.getLastModificationTime() > b.getLastModificationTime();
        });

        for (const auto& rig : rigs)
            mTiles.push_back({Tile::rig, rig, rig.getFileNameWithoutExtension(),
                              rig.getLastModificationTime().toString(true, false, false), {}});
        mTiles.push_back({Tile::newRig, {}, "+ New rig", "", {}});

        // --- Setlists shelf.
        for (const auto& setlist : Setlist::findAll())
            mTiles.push_back({Tile::setlist, setlist, setlist.getFileNameWithoutExtension(), "", {}});

        // --- Tools shelf.
        mTiles.push_back({Tile::tool, {}, "Audio settings", "audio", {}});
        mTiles.push_back({Tile::tool, {}, "TONE3000", "tone3000", {}});
        mTiles.push_back({Tile::tool, {}, "Rescan plug-ins", "rescan", {}});
        mTiles.push_back({Tile::tool, {}, "Rigs folder", "folder", {}});

        layoutTiles();
        repaint();
    }

    void resized() override { layoutTiles(); }

    void paint(juce::Graphics& g) override
    {
        // 5a gradient backdrop.
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff121017), 0.0f, 0.0f,
                                               theme::colours::background, 0.0f,
                                               static_cast<float>(getHeight()), false));
        g.fillAll();

        // Top bar.
        auto top = getLocalBounds().removeFromTop(64).reduced(theme::metrics::padding + 6, 0).toFloat();

        theme::drawLogoMark(g, top.removeFromLeft(30.0f).reduced(0.0f, 19.0f));
        top.removeFromLeft(10.0f);
        g.setColour(theme::colours::text);
        g.setFont(theme::fonts::ui(17.0f, 700));
        g.drawText("BlockRig", top.removeFromLeft(110.0f), juce::Justification::centredLeft, false);

        // Device status: green dot when a device is open, plus round-trip-ish
        // latency, which is the number a live player actually cares about.
        if (mDeviceManager != nullptr)
        {
            auto status = top.removeFromRight(300.0f);

            if (auto* device = mDeviceManager->getCurrentAudioDevice())
            {
                const auto latencyMs = (device->getOutputLatencyInSamples()
                                        + device->getCurrentBufferSizeSamples())
                                       / device->getCurrentSampleRate() * 1000.0;

                g.setColour(theme::colours::good);
                g.fillEllipse(status.getX(), status.getCentreY() - 3.5f, 7.0f, 7.0f);
                g.setColour(theme::colours::textFaint);
                g.setFont(theme::fonts::ui(13.0f));
                g.drawText(" " + device->getName() + juce::String::fromUTF8("  ·  ") + juce::String(latencyMs, 1) + " ms",
                           status.withTrimmedLeft(10.0f), juce::Justification::centredLeft, true);
            }
            else
            {
                g.setColour(theme::colours::textGhost);
                g.setFont(theme::fonts::ui(13.0f));
                g.drawText("No audio device", status, juce::Justification::centredLeft, false);
            }
        }

        // Shelf labels.
        g.setFont(theme::fonts::ui(15.0f, 500));
        for (const auto& shelf : mShelfLabels)
        {
            g.setColour(theme::colours::textFaint);
            g.drawText(shelf.first, shelf.second, juce::Justification::bottomLeft, false);
        }

        // Unfocused tiles first, so the focused one paints over its neighbours.
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < static_cast<int>(mTiles.size()); ++i)
                if ((i == mHover) == (pass == 1))
                    paintTile(g, mTiles[static_cast<size_t>(i)], i == mHover);
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        const auto hover = tileAt(event.getPosition());

        if (hover != mHover)
        {
            mHover = hover;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        mHover = -1;
        repaint();
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        const auto index = tileAt(event.getPosition());
        if (index < 0)
            return;

        const auto tile = mTiles[static_cast<size_t>(index)]; // copy — see below

        if (event.mods.isPopupMenu() && tile.kind == Tile::rig)
        {
            showRigMenu(tile.file);
            return;
        }

        // Opening a rig or setlist destroys the home screen — and this
        // component, and the tile, with it — so the action runs on the next
        // message-loop tick, outside the mouse-up, with its own copy of the
        // tile. (This was a real crash: File::existsAsFile on freed memory.)
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer(this), tile] {
                if (safe != nullptr)
                    safe->activate(tile);
            });
    }

private:
    struct Tile
    {
        enum Kind
        {
            rig,
            newRig,
            setlist,
            tool
        } kind;

        juce::File file;
        juce::String title;
        juce::String meta; ///< rig: date · tool: action id
        juce::Rectangle<float> bounds;
    };

    void layoutTiles()
    {
        mShelfLabels.clear();

        const auto left = static_cast<float>(theme::metrics::padding + 6);
        float y = 96.0f;

        const auto lay = [&](Tile::Kind kind, const char* label, float width, float height) {
            mShelfLabels.push_back(
                {label, juce::Rectangle<int>(static_cast<int>(left), static_cast<int>(y) - 28, 300, 22)});
            float x = left;

            for (auto& tile : mTiles)
            {
                if (tile.kind != kind && !(kind == Tile::rig && tile.kind == Tile::newRig))
                    continue;

                tile.bounds = {x, y, width, height};
                x += width + 18.0f;
            }

            y += height + 60.0f;
        };

        lay(Tile::rig, "Rigs", 300.0f, 176.0f);
        lay(Tile::setlist, "Setlists", 224.0f, 120.0f);
        lay(Tile::tool, "Tools", 150.0f, 96.0f);
    }

    int tileAt(juce::Point<int> position) const
    {
        for (int i = 0; i < static_cast<int>(mTiles.size()); ++i)
            if (mTiles[static_cast<size_t>(i)].bounds.contains(position.toFloat()))
                return i;
        return -1;
    }

    void paintTile(juce::Graphics& g, const Tile& tile, bool focused)
    {
        auto bounds = tile.bounds;
        if (focused)
            bounds = bounds.withSizeKeepingCentre(bounds.getWidth() * 1.06f, bounds.getHeight() * 1.06f);

        const auto alpha = (mHover >= 0 && !focused) ? 0.85f : 1.0f;
        const auto radius = theme::metrics::radiusXl;

        if (focused)
        {
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.fillRoundedRectangle(bounds.translated(0.0f, 10.0f).expanded(4.0f), radius + 2.0f);
        }

        if (tile.kind == Tile::newRig)
        {
            juce::Path dashes;
            dashes.addRoundedRectangle(bounds, radius);
            const float pattern[] = {6.0f, 5.0f};
            juce::PathStrokeType(1.5f).createDashedStroke(dashes, dashes, pattern, 2);
            g.setColour(theme::colours::outlineStrong.withAlpha(alpha));
            g.fillPath(dashes);
            g.setColour(theme::colours::textFaint.withAlpha(alpha));
            g.setFont(theme::fonts::ui(17.0f, 500));
            g.drawText(tile.title, bounds, juce::Justification::centred, false);
            return;
        }

        g.setColour(theme::colours::panel.withAlpha(alpha));
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(focused ? theme::colours::text.withAlpha(0.9f)
                            : juce::Colour(0xff26232f).withAlpha(alpha));
        g.drawRoundedRectangle(bounds, radius, focused ? 3.0f : 1.0f);

        auto inner = bounds.reduced(16.0f, 14.0f);

        if (tile.kind == Tile::rig)
        {
            // Category dots: which kinds of blocks this rig carries.
            auto dots = inner.removeFromTop(16.0f);
            float x = dots.getX();

            for (const auto& colour : rigCategoryColours(tile.file))
            {
                g.setColour(colour.withAlpha(alpha));
                g.fillRoundedRectangle(x, dots.getY(), 13.0f, 13.0f, 4.5f);
                x += 18.0f;
            }

            g.setColour(theme::colours::text.withAlpha(alpha));
            g.setFont(theme::fonts::ui(21.0f, 700));
            g.drawFittedText(tile.title,
                             inner.withTrimmedTop(inner.getHeight() * 0.35f)
                                 .removeFromTop(30.0f)
                                 .toNearestInt(),
                             juce::Justification::centredLeft, 1);

            g.setColour(theme::colours::textFaint.withAlpha(alpha));
            g.setFont(theme::fonts::ui(13.0f));
            g.drawText(tile.meta, inner, juce::Justification::bottomLeft, false);
        }
        else if (tile.kind == Tile::setlist)
        {
            g.setColour(theme::colours::accent.withAlpha(alpha));
            g.setFont(theme::fonts::mono(11.0f, 500));
            g.drawText("SET", inner.removeFromTop(16.0f), juce::Justification::topLeft, false);

            g.setColour(theme::colours::text.withAlpha(alpha));
            g.setFont(theme::fonts::ui(17.0f, 700));
            g.drawFittedText(tile.title, inner.toNearestInt(), juce::Justification::bottomLeft, 2);
        }
        else // tool
        {
            g.setColour(theme::colours::accent.withAlpha(0.85f * alpha));
            const auto icon =
                inner.removeFromTop(inner.getHeight() - 22.0f).withSizeKeepingCentre(26.0f, 26.0f);
            g.drawRoundedRectangle(icon, 6.0f, 2.0f);
            g.fillEllipse(icon.reduced(8.0f));

            g.setColour(theme::colours::textDim.withAlpha(alpha));
            g.setFont(theme::fonts::ui(13.0f, 500));
            g.drawText(tile.title, bounds.withTrimmedTop(bounds.getHeight() - 30.0f),
                       juce::Justification::centred, false);
        }
    }

    /// Up to five category colours from a rig file, without instantiating
    /// anything: the saved block names are enough to categorise, and the result
    /// is cached because paint() runs on every hover change.
    juce::Array<juce::Colour> rigCategoryColours(const juce::File& file)
    {
        const auto key = file.getFullPathName().hashCode64();

        if (const auto cached = mDotCache.find(key); cached != mDotCache.end())
            return cached->second;

        juce::Array<juce::Colour> colours;

        if (const auto xml = juce::parseXML(file.loadFileAsString()))
        {
            const auto tree = juce::ValueTree::fromXml(*xml);
            const auto lane = tree.getChildWithName(rigstate::ids::lane);

            for (const auto& stage : lane)
                for (const auto& row : stage)
                    for (const auto& block : row)
                    {
                        if (colours.size() >= 5)
                            break;

                        const auto colour = theme::colourForCategory(
                            {}, block.getProperty(rigstate::ids::name).toString());

                        if (!colours.contains(colour))
                            colours.add(colour);
                    }
        }

        mDotCache[key] = colours;
        return colours;
    }

    void activate(const Tile& tile)
    {
        switch (tile.kind)
        {
            case Tile::rig: mShell.openRig(tile.file); break;
            case Tile::setlist: mShell.openSetlist(tile.file); break;
            case Tile::newRig: createRig(); break;
            case Tile::tool:
                if (tile.meta == "audio")
                    showAudioSettings();
                else if (tile.meta == "rescan")
                    mShell.beginBoot();
                else if (tile.meta == "folder")
                    MainView::getRigsFolder().revealToUser();
                else if (tile.meta == "tone3000")
                    showTone3000();
                break;
        }
    }

    void createRig()
    {
        auto window = std::make_shared<juce::AlertWindow>("New rig", "", juce::MessageBoxIconType::NoIcon,
                                                          this);
        window->setAlwaysOnTop(true);
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

            mProcessor.notifyBlockRemoval({});
            mProcessor.getChain().clear();
            mProcessor.getSnapshots().getSnapshots().clear();
            mProcessor.getSnapshots().activeIndex = -1;

            juce::String error;
            rigfiles::save(mProcessor, file, error);
            mShell.openRig(file);
        }));
    }

    void showRigMenu(const juce::File& file)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Open");
        menu.addItem(2, "Rename...");
        menu.addItem(3, "Delete...");

        menu.showMenuAsync(juce::PopupMenu::Options(), [this, file](int choice) {
            if (choice == 1)
            {
                mShell.openRig(file);
            }
            else if (choice == 2)
            {
                auto window = std::make_shared<juce::AlertWindow>("Rename rig", "",
                                                                  juce::MessageBoxIconType::NoIcon, this);
                window->setAlwaysOnTop(true);
                window->addTextEditor("name", file.getFileNameWithoutExtension());
                window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
                window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

                window->enterModalState(
                    true, juce::ModalCallbackFunction::create([this, file, window](int result) {
                        const auto text = juce::File::createLegalFileName(
                            window->getTextEditorContents("name").trim());

                        if (result == 1 && text.isNotEmpty())
                        {
                            const auto target =
                                file.getSiblingFile(text + juce::String(rigfiles::kFileExtension));
                            if (!target.existsAsFile())
                                file.moveFileTo(target);
                        }

                        mDotCache.clear();
                        refresh();
                    }));
            }
            else if (choice == 3)
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
                        mDotCache.clear();
                        refresh();
                    });
            }
        });
    }

    void showAudioSettings()
    {
        if (mDeviceManager == nullptr)
            return;

        auto* selector = new juce::AudioDeviceSelectorComponent(*mDeviceManager, 1, 2, 0, 2, false, false,
                                                                false, false);
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

    void showTone3000()
    {
        auto* panel = new Tone3000Panel();
        panel->setSize(Tone3000Panel::kPreferredWidth, Tone3000Panel::kPreferredHeight);

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(panel);
        options.dialogTitle = "TONE3000";
        options.dialogBackgroundColour = theme::colours::background;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }

    AppShell& mShell;
    BlockRigProcessor& mProcessor;
    juce::AudioDeviceManager* mDeviceManager;

    std::vector<Tile> mTiles;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> mShelfLabels;
    std::map<juce::int64, juce::Array<juce::Colour>> mDotCache;
    int mHover = -1;
};

//==============================================================================
AppShell::AppShell(BlockRigProcessor& processor, juce::AudioDeviceManager* deviceManager)
    : mProcessor(processor)
    , mDeviceManager(deviceManager)
{
    juce::LookAndFeel::setDefaultLookAndFeel(&mLook);
    setOpaque(true);
    setSize(1440, 820);
}

AppShell::~AppShell()
{
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);

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
        mProgressText = "";
        mProgressScanned = 0;
        mProgressTotal = 0;
    }

    mScanThread = std::thread([this] { scanThreadBody(); });
    startTimerHz(60); // the fly-in wants real frames
    repaint();
}

void AppShell::scanThreadBody()
{
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
        mProgressText = progress.currentPluginName;
        mProgressScanned = progress.scanned;
        mProgressTotal = progress.total;
    });

    catalog.saveToStorage();
    mScanDone.store(true);
}

void AppShell::timerCallback()
{
    ++mBootElapsedTicks;

    if (mScreen == Screen::boot)
    {
        // Let the lockup land (the animation runs ~1.4 s), then leave as soon
        // as the sweep is done.
        if (mScanDone.load() && mBootElapsedTicks >= 90)
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

void AppShell::openRig(juce::File file)
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

void AppShell::openSetlist(juce::File setlistFile)
{
    // Open the rig screen, hand it the setlist, then load the set's first rig.
    openRig({});

    if (mMainView != nullptr)
        if (const auto first = mMainView->activateSetlist(setlistFile); first.existsAsFile())
            mMainView->loadRigFile(first);
}

void AppShell::requestQuit(std::function<void()> quitNow)
{
    if (mMainView != nullptr)
        mMainView->confirmThenSwitch(std::move(quitNow));
    else
        quitNow();
}

//==============================================================================
void AppShell::paint(juce::Graphics& g)
{
    // Radial-tinted black, per the boot spec.
    juce::ColourGradient backdrop(juce::Colour(0xff12101a), getWidth() * 0.5f, getHeight() * 0.42f,
                                  theme::colours::background, 0.0f, 0.0f, true);
    g.setGradientFill(backdrop);
    g.fillAll();

    if (mScreen != Screen::boot)
        return;

    const auto seconds = static_cast<float>(mBootElapsedTicks) / 60.0f;
    const auto centre = juce::Point<float>(getWidth() * 0.5f, getHeight() * 0.42f);

    // --- The mark, assembled from the four corners; amber lands last.
    constexpr float logoSize = 76.0f;
    const auto gapPx = logoSize * 0.12f;
    const auto square = (logoSize - gapPx) * 0.5f;
    const auto radius = square * 0.28f;
    const auto stroke = square / 9.0f;
    const auto origin = juce::Point<float>(centre.x - logoSize * 0.5f, centre.y - logoSize - 8.0f);

    struct Flight
    {
        int column, row;
        float delay;
        juce::Point<float> from; ///< as a fraction of the window
        bool amber;
    };

    const Flight flights[] = {
        {0, 0, 0.10f, {-0.2f, -0.2f}, false},
        {1, 0, 0.25f, {1.2f, -0.2f}, false},
        {0, 1, 0.40f, {-0.2f, 1.2f}, false},
        {1, 1, 0.60f, {1.2f, 1.2f}, true},
    };

    for (const auto& flight : flights)
    {
        const auto t = easeOut((seconds - flight.delay) / 0.75f);
        if (t <= 0.0f)
            continue;

        const juce::Point<float> home(origin.x + flight.column * (square + gapPx),
                                      origin.y + flight.row * (square + gapPx));
        const juce::Point<float> start(flight.from.x * getWidth(), flight.from.y * getHeight());
        const auto position = start + (home - start) * t;
        const juce::Rectangle<float> cell(position.x, position.y, square, square);

        if (flight.amber)
        {
            g.setColour(theme::colours::accent.withAlpha(0.30f * t));
            g.fillRoundedRectangle(cell.expanded(7.0f * t), radius + 3.0f);
            g.setColour(theme::colours::accent);
            g.fillRoundedRectangle(cell, radius);
        }
        else
        {
            g.setColour(theme::colours::outlineStrong);
            g.drawRoundedRectangle(cell.reduced(stroke * 0.5f), radius, stroke);
        }
    }

    // --- Wordmark and strap fade up from 1.0 s.
    if (const auto fade = easeOut((seconds - 1.0f) / 0.4f); fade > 0.0f)
    {
        juce::Graphics::ScopedSaveState save(g);
        g.setOpacity(fade);

        theme::drawWordmark(
            g, juce::Rectangle<float>(0.0f, centre.y + 6.0f, static_cast<float>(getWidth()), 44.0f),
            30.0f);
        g.setColour(theme::colours::textGhost);
        g.setFont(theme::fonts::ui(13.0f));
        g.drawText("Live VST host",
                   juce::Rectangle<float>(0.0f, centre.y + 52.0f, static_cast<float>(getWidth()), 20.0f),
                   juce::Justification::centred, false);
    }

    // --- Progress: 380 px bar, plug-in name left, count right, both mono.
    if (seconds > 1.2f)
    {
        juce::String name;
        int scanned = 0, total = 0;
        {
            const juce::ScopedLock lock(mProgressLock);
            name = mProgressText;
            scanned = mProgressScanned;
            total = mProgressTotal;
        }

        const juce::Rectangle<float> track(centre.x - 190.0f, centre.y + 110.0f, 380.0f, 4.0f);
        g.setColour(theme::colours::hairline);
        g.fillRoundedRectangle(track, 2.0f);

        if (total > 0)
        {
            const auto fraction =
                juce::jlimit(0.0f, 1.0f, static_cast<float>(scanned) / static_cast<float>(total));
            juce::ColourGradient amber(theme::colours::accentBright, track.getX(), 0.0f,
                                       theme::colours::accent, track.getRight(), 0.0f, false);
            g.setGradientFill(amber);
            g.fillRoundedRectangle(track.withWidth(juce::jmax(4.0f, track.getWidth() * fraction)), 2.0f);
        }

        g.setFont(theme::fonts::mono(12.0f));
        g.setColour(theme::colours::textGhost);

        const auto caption = track.translated(0.0f, 12.0f).withHeight(18.0f);
        g.drawText(total > 0 ? "Scanning plug-ins...  " + name : juce::String("Checking plug-ins..."),
                   caption.withTrimmedRight(90.0f), juce::Justification::centredLeft, true);

        if (total > 0)
            g.drawText(juce::String(scanned) + " / " + juce::String(total), caption,
                       juce::Justification::centredRight, false);
    }
}

void AppShell::resized()
{
    if (mHome != nullptr)
        mHome->setBounds(getLocalBounds());
    if (mMainView != nullptr)
        mMainView->setBounds(getLocalBounds());
}

} // namespace blockrig
