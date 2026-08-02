#include "ui/SnapshotStrip.h"

#include "ui/BlockCategories.h"
#include "ui/BlockWindow.h"
#include "ui/Theme.h"

namespace blockrig
{

//==============================================================================
/// One snapshot chip. Drawn, not a TextButton, so active/edit states can look
/// like part of the strip rather than stock buttons.
class SnapshotStrip::Chip final : public juce::Component
{
public:
    Chip(juce::String name, int index)
        : mName(std::move(name))
        , mIndex(index)
    {
    }

    void setStates(bool active, bool editMode)
    {
        mActive = active;
        mEditMode = editMode;
        repaint();
    }

    int getIdealWidth() const
    {
        return juce::jlimit(64, 150, 26 + juce::GlyphArrangement::getStringWidthInt(
                                              juce::FontOptions(12.5f), mName));
    }

    std::function<void(int)> onClick, onMenu;

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (!contains(event.getPosition()))
            return;

        if (event.mods.isPopupMenu())
        {
            if (onMenu)
                onMenu(mIndex);
        }
        else if (onClick)
        {
            onClick(mIndex);
        }
    }

    void setIndexLetter(int index) { mLetter = juce::String::charToString(static_cast<juce::juce_wchar>('A' + (index % 26))); }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f, 4.0f);
        const auto radius = 12.0f;

        if (mActive)
        {
            // Active pad: amber 12% fill, 2px border, glow.
            g.setColour(theme::colours::accent.withAlpha(0.18f));
            g.fillRoundedRectangle(bounds.expanded(2.0f), radius + 2.0f);
            g.setColour(theme::colours::accent.withAlpha(0.12f));
            g.fillRoundedRectangle(bounds, radius);
        }
        else
        {
            g.setColour(theme::colours::panel);
            g.fillRoundedRectangle(bounds, radius);
        }

        if (mEditMode)
        {
            juce::Path outline;
            outline.addRoundedRectangle(bounds, radius);
            const float dashes[] = {4.0f, 3.0f};
            juce::PathStrokeType(1.4f).createDashedStroke(outline, outline, dashes, 2);
            g.setColour(theme::colours::warn);
            g.fillPath(outline);
        }
        else
        {
            g.setColour(mActive ? theme::colours::accent : theme::colours::outline);
            g.drawRoundedRectangle(bounds, radius, mActive ? 2.0f : 1.0f);
        }

        auto inner = bounds.reduced(10.0f, 3.0f);

        // Letter in mono, then the name: the letter is what MIDI PC numbers map
        // to, so it earns its place on the pad.
        g.setColour(mActive ? theme::colours::accent : theme::colours::textGhost);
        g.setFont(theme::fonts::mono(11.0f, 500));
        g.drawText(mLetter, inner.removeFromLeft(14.0f), juce::Justification::centred, false);

        g.setColour(mActive ? theme::colours::text : theme::colours::textDim);
        g.setFont(theme::fonts::ui(14.0f, 700));
        g.drawText(mName, inner.withTrimmedLeft(4.0f), juce::Justification::centredLeft, true);
    }

private:
    juce::String mName;
    juce::String mLetter;
    int mIndex;
    bool mActive = false;
    bool mEditMode = false;
};

//==============================================================================
/// The save-scene dialog (4h): a name, and exactly what this scene saves.
///
/// The checklist defaults to everything a scene CAN hold - every block's
/// parameters (for the NAM that includes the loaded capture), the tempo, and
/// the tuner. What it can never hold is the rig's structure: adding, removing
/// or reordering blocks belongs to rigs, not scenes.
class SnapshotStrip::AddPanel final : public juce::Component
{
public:
    /// One checklist row: 18px amber checkbox, label, and the block's category
    /// dot on the right edge. Included rows get the raised fill.
    class Row final : public juce::Component
    {
    public:
        Row(juce::String label, bool on, juce::Colour dot, juce::String uid)
            : mLabel(std::move(label))
            , mUid(std::move(uid))
            , mDot(dot)
            , mOn(on)
        {
        }

        bool isOn() const { return mOn; }
        const juce::String& getUid() const { return mUid; }

        void mouseUp(const juce::MouseEvent& event) override
        {
            if (!contains(event.getPosition()))
                return;
            mOn = !mOn;
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat();

            if (mOn)
            {
                g.setColour(theme::colours::panelRaised);
                g.fillRoundedRectangle(bounds, theme::metrics::radiusMd);
            }

            const juce::Rectangle<float> box{12.0f, bounds.getCentreY() - 9.0f, 18.0f, 18.0f};

            if (mOn)
            {
                g.setColour(theme::colours::accent);
                g.fillRoundedRectangle(box, 5.0f);

                juce::Path tick;
                tick.startNewSubPath(box.getX() + 4.5f, box.getCentreY() + 0.5f);
                tick.lineTo(box.getCentreX() - 0.5f, box.getBottom() - 5.0f);
                tick.lineTo(box.getRight() - 4.0f, box.getY() + 5.0f);
                g.setColour(theme::colours::onAccent);
                g.strokePath(tick, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
            }
            else
            {
                g.setColour(theme::colours::outlineStrong);
                g.drawRoundedRectangle(box.reduced(0.75f), 5.0f, 1.5f);
            }

            g.setColour(mOn ? theme::colours::text : theme::colours::textFaint);
            g.setFont(theme::fonts::ui(15.0f, mOn ? 600 : 400));
            g.drawText(mLabel, getLocalBounds().withTrimmedLeft(42).withTrimmedRight(30),
                       juce::Justification::centredLeft, true);

            if (!mDot.isTransparent())
            {
                g.setColour(mDot);
                g.fillRoundedRectangle(bounds.getRight() - 22.0f, bounds.getCentreY() - 5.0f, 10.0f,
                                       10.0f, 3.0f);
            }
        }

    private:
        juce::String mLabel, mUid;
        juce::Colour mDot;
        bool mOn;
    };

    /// `editIndex` >= 0 edits an existing scene's name and checklist instead of
    /// creating one, so a scene can be re-scoped without rebuilding it.
    AddPanel(BlockRigProcessor& processor, std::function<void()> onSaved, int editIndex = -1)
        : mProcessor(processor)
        , mOnSaved(std::move(onSaved))
        , mEditIndex(editIndex)
    {
        const auto styleCaption = [](juce::Label& label, const juce::String& text) {
            label.setText(text, juce::dontSendNotification);
            label.setFont(theme::fonts::ui(11.0f, 500));
            label.setColour(juce::Label::textColourId, theme::colours::textFaint);
        };

        styleCaption(mNameLabel, "Name");
        addAndMakeVisible(mNameLabel);

        const auto& existing = mProcessor.getSnapshots().getSnapshots();
        const bool editing = mEditIndex >= 0 && mEditIndex < static_cast<int>(existing.size());

        mName.setFont(theme::fonts::ui(16.0f, 600));
        mName.setText(editing ? existing[static_cast<size_t>(mEditIndex)].name
                              : "Scene " + juce::String(static_cast<int>(existing.size() + 1)));
        mName.setSelectAllWhenFocused(true);
        addAndMakeVisible(mName);

        styleCaption(mSafesLabel, "Saved in this scene");
        addAndMakeVisible(mSafesLabel);

        // Everything defaults on for a new scene; editing shows what this one
        // actually holds.
        for (auto* block : mProcessor.getChain().getBlocks())
        {
            const bool ticked =
                !editing
                || existing[static_cast<size_t>(mEditIndex)].blockStates.count(block->getUid()) > 0;

            juce::PluginDescription description = block->getMissingDescription();
            if (auto* plugin = block->getPlugin())
                plugin->fillInPluginDescription(description);

            auto row = std::make_unique<Row>(block->getDisplayName(), ticked,
                                             getCategoryColour(categoriseBlock(description)),
                                             block->getUid());
            mHolder.addAndMakeVisible(*row);
            mBlockRows.push_back(std::move(row));
        }

        mTempoRow = std::make_unique<Row>("Tempo & time signature",
                                          !editing
                                              || existing[static_cast<size_t>(mEditIndex)].includeTempo,
                                          juce::Colours::transparentBlack, juce::String());
        mHolder.addAndMakeVisible(*mTempoRow);

        mTunerRow = std::make_unique<Row>("Tuner",
                                          !editing
                                              || existing[static_cast<size_t>(mEditIndex)].includeTuner,
                                          juce::Colours::transparentBlack, juce::String());
        mHolder.addAndMakeVisible(*mTunerRow);

        mViewport.setViewedComponent(&mHolder, false);
        mViewport.setScrollBarsShown(true, false);
        addAndMakeVisible(mViewport);

        mCancel.onClick = [this] { close(); };
        addAndMakeVisible(mCancel);

        mSave.setButtonText(editing ? "Update scene" : "Save scene");
        mSave.getProperties().set("primary", true);
        mSave.onClick = [this] { save(); };
        addAndMakeVisible(mSave);

        const auto rows = static_cast<int>(mBlockRows.size()) + 2;
        setSize(460, 176 + juce::jmin(6, rows) * kRowStep);
    }

    void parentHierarchyChanged() override
    {
        // A save dialog has no business being pinned open.
        if (auto* window = findParentComponentOfClass<BlockWindow>())
            window->setPinnable(false);
    }

    void paint(juce::Graphics& g) override { g.fillAll(theme::colours::background); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(theme::metrics::padding, theme::metrics::gap);

        mNameLabel.setBounds(area.removeFromTop(16));
        area.removeFromTop(4);
        mName.setBounds(area.removeFromTop(40));
        area.removeFromTop(12);
        mSafesLabel.setBounds(area.removeFromTop(16));
        area.removeFromTop(6);

        auto footer = area.removeFromBottom(40);
        mSave.setBounds(footer.removeFromRight(130).reduced(0, 2));
        footer.removeFromRight(10);
        mCancel.setBounds(footer.removeFromRight(96).reduced(0, 2));

        area.removeFromBottom(10);
        mViewport.setBounds(area);

        const auto rowWidth = mViewport.getMaximumVisibleWidth();
        mHolder.setSize(rowWidth, kRowStep * (static_cast<int>(mBlockRows.size()) + 2));

        int y = 0;
        const auto place = [&](Row& row) {
            row.setBounds(0, y, rowWidth, kRowHeight);
            y += kRowStep;
        };

        for (auto& row : mBlockRows)
            place(*row);
        place(*mTempoRow);
        place(*mTunerRow);
    }

private:
    static constexpr int kRowHeight = 44;
    static constexpr int kRowStep = kRowHeight + 4;

    void close()
    {
        // Hosted inside a BlockWindow; its close callback tears us down.
        if (auto* window = findParentComponentOfClass<BlockWindow>())
            if (window->onClose)
                window->onClose();
    }

    void save()
    {
        juce::StringArray uids;
        for (const auto& row : mBlockRows)
            if (row->isOn())
                uids.add(row->getUid());

        auto name = mName.getText().trim();
        if (name.isEmpty())
            name = "Scene";

        auto& bank = mProcessor.getSnapshots();
        auto captured = snapshots::Bank::capture(mProcessor, name, uids, mTempoRow->isOn(),
                                                 mTunerRow->isOn());

        if (mEditIndex >= 0 && mEditIndex < static_cast<int>(bank.getSnapshots().size()))
        {
            bank.getSnapshots()[static_cast<size_t>(mEditIndex)] = std::move(captured);
            bank.activeIndex = mEditIndex;
        }
        else
        {
            bank.getSnapshots().push_back(std::move(captured));
            bank.activeIndex = static_cast<int>(bank.getSnapshots().size()) - 1;
        }

        if (mOnSaved)
            mOnSaved();

        close();
    }

    BlockRigProcessor& mProcessor;
    std::function<void()> mOnSaved;

    juce::Label mNameLabel, mSafesLabel;
    juce::TextEditor mName;
    juce::Viewport mViewport;
    juce::Component mHolder;
    std::vector<std::unique_ptr<Row>> mBlockRows;
    std::unique_ptr<Row> mTempoRow, mTunerRow;
    juce::TextButton mCancel{"Cancel"};
    juce::TextButton mSave{"Save scene"};
    int mEditIndex = -1;
};

//==============================================================================
SnapshotStrip::SnapshotStrip(BlockRigProcessor& processor)
    : mProcessor(processor)
{
    mAddButton.setTooltip("Save the rig's current settings as a snapshot");
    mAddButton.onClick = [this] { showAddPanel(); };
    addAndMakeVisible(mAddButton);

    mEditToggle.setClickingTogglesState(true);
    mEditToggle.setTooltip("Edit mode: clicking a snapshot saves the current settings into it "
                           "instead of applying it.");
    mEditToggle.onClick = [this] {
        mEditMode = mEditToggle.getToggleState();
        refresh();
    };
    addAndMakeVisible(mEditToggle);

    rebuildChips();
}

SnapshotStrip::~SnapshotStrip() = default;

void SnapshotStrip::refresh()
{
    rebuildChips();
}

void SnapshotStrip::rebuildChips()
{
    mChips.clear();

    const auto& bank = mProcessor.getSnapshots();
    int index = 0;

    for (const auto& snapshot : bank.getSnapshots())
    {
        auto chip = std::make_unique<Chip>(snapshot.name, index);
        chip->setIndexLetter(index);
        chip->setStates(index == bank.activeIndex, mEditMode);
        chip->onClick = [this](int chipIndex) { chipClicked(chipIndex); };
        chip->onMenu = [this](int chipIndex) { showChipMenu(chipIndex); };
        addAndMakeVisible(*chip);
        mChips.push_back(std::move(chip));
        ++index;
    }

    resized();
    repaint();
}

void SnapshotStrip::chipClicked(int index)
{
    if (mEditMode)
        overwriteSnapshot(index);
    else
        applySnapshot(index);
}

void SnapshotStrip::applySnapshot(int index)
{
    auto& bank = mProcessor.getSnapshots();
    if (index < 0 || index >= static_cast<int>(bank.getSnapshots().size()))
        return;

    const auto& snapshot = bank.getSnapshots()[static_cast<size_t>(index)];
    snapshots::Bank::apply(mProcessor, snapshot);
    bank.activeIndex = index;

    if (snapshot.includeTuner && onTunerRecalled)
        onTunerRecalled(snapshot.tunerActive);

    if (onBankChanged)
        onBankChanged();

    refresh();
}

void SnapshotStrip::overwriteSnapshot(int index)
{
    auto& bank = mProcessor.getSnapshots();
    if (index < 0 || index >= static_cast<int>(bank.getSnapshots().size()))
        return;

    const auto name = bank.getSnapshots()[static_cast<size_t>(index)].name;

    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Save into \"" + name + "\"?")
            .withMessage("The rig's current settings will replace what this snapshot holds.")
            .withButton("Save")
            .withButton("Cancel")
            .withAssociatedComponent(this),
        [this, index](int result) {
            if (result != 1)
                return;

            auto& bank = mProcessor.getSnapshots();
            if (index >= static_cast<int>(bank.getSnapshots().size()))
                return;

            auto& snapshot = bank.getSnapshots()[static_cast<size_t>(index)];

            // Keep the snapshot's own choices: same name, same safes; only the
            // captured values change.
            juce::StringArray uids;
            for (const auto& [uid, state] : snapshot.blockStates)
                uids.add(uid);

            auto refreshed = snapshots::Bank::capture(mProcessor, snapshot.name, uids,
                                                      snapshot.includeTempo, snapshot.includeTuner);
            snapshot = std::move(refreshed);
            bank.activeIndex = index;

            if (onBankChanged)
                onBankChanged();

            refresh();
        });
}

void SnapshotStrip::showChipMenu(int index)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Rename...");
    menu.addItem(3, "Edit what's saved...");
    menu.addItem(2, "Delete");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, index](int choice) {
        auto& bank = mProcessor.getSnapshots();
        if (index >= static_cast<int>(bank.getSnapshots().size()))
            return;

        if (choice == 1)
        {
            auto window = std::make_shared<juce::AlertWindow>(
                "Rename snapshot", "", juce::MessageBoxIconType::NoIcon, this);
            window->setAlwaysOnTop(true);
            window->addTextEditor("name", bank.getSnapshots()[static_cast<size_t>(index)].name);
            window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
            window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

            window->enterModalState(true, juce::ModalCallbackFunction::create([this, index, window](int result) {
                if (result == 1)
                {
                    auto& innerBank = mProcessor.getSnapshots();
                    if (index < static_cast<int>(innerBank.getSnapshots().size()))
                    {
                        const auto text = window->getTextEditorContents("name").trim();
                        if (text.isNotEmpty())
                            innerBank.getSnapshots()[static_cast<size_t>(index)].name = text;
                    }
                    if (onBankChanged)
                        onBankChanged();
                    refresh();
                }
            }));
        }
        else if (choice == 3)
        {
            if (openPanel)
            {
                auto panel = std::make_unique<AddPanel>(mProcessor,
                                                        [this] {
                                                            if (onBankChanged)
                                                                onBankChanged();
                                                            refresh();
                                                        },
                                                        index);
                const auto width = panel->getWidth();
                const auto height = panel->getHeight();
                openPanel(std::move(panel), "Edit scene", width, height);
            }
        }
        else if (choice == 2)
        {
            bank.getSnapshots().erase(bank.getSnapshots().begin() + index);
            if (bank.activeIndex == index)
                bank.activeIndex = -1;
            else if (bank.activeIndex > index)
                --bank.activeIndex;

            if (onBankChanged)
                onBankChanged();
            refresh();
        }
    });
}

void SnapshotStrip::showAddPanel()
{
    if (!openPanel)
        return;

    auto panel = std::make_unique<AddPanel>(mProcessor, [this] {
        if (onBankChanged)
            onBankChanged();
        refresh();
    });

    const auto width = panel->getWidth();
    const auto height = panel->getHeight();
    openPanel(std::move(panel), "Save scene", width, height);
}

void SnapshotStrip::paint(juce::Graphics& g)
{
    g.setColour(theme::colours::textFaint);
    g.setFont(theme::fonts::ui(13.0f, 500));
    g.drawText("Scenes", getLocalBounds().removeFromLeft(74).reduced(theme::metrics::padding, 0),
               juce::Justification::centredLeft, false);

    if (mChips.empty())
    {
        g.setColour(theme::colours::textGhost);
        g.setFont(theme::fonts::ui(12.5f));
        // fromUTF8: a bare char* literal is read as Latin-1, turning the dash
        // into mojibake.
        g.drawText(juce::String::fromUTF8("none yet \xe2\x80\x94 press  +  to save the current "
                                          "settings as a scene"),
                   getLocalBounds().withTrimmedLeft(120), juce::Justification::centredLeft, false);
    }
}

void SnapshotStrip::resized()
{
    auto area = getLocalBounds().reduced(theme::metrics::padding, 6);
    area.removeFromLeft(78); // the "Scenes" caption

    mEditToggle.setBounds(area.removeFromRight(64).reduced(0, 3));
    area.removeFromRight(8);

    mAddButton.setBounds(area.removeFromLeft(38).reduced(0, 3));
    area.removeFromLeft(8);

    // Pads share the row equally — 4c's flex:1 — so the scene row spans the
    // window instead of trailing off into empty space on the right.
    if (!mChips.empty())
    {
        const auto count = static_cast<int>(mChips.size());
        const auto gaps = 8 * (count - 1);
        const auto pad = juce::jmax(96, (area.getWidth() - gaps) / count);

        for (int i = 0; i < count; ++i)
        {
            mChips[static_cast<size_t>(i)]->setBounds(
                i == count - 1 ? area : area.removeFromLeft(pad));
            area.removeFromLeft(8);
        }
    }
}

} // namespace blockrig
