#include "ui/SnapshotStrip.h"

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
/// The "new snapshot" panel: a name, and exactly what this snapshot saves.
///
/// The safes list defaults to everything a snapshot CAN hold - every block's
/// parameters (for the NAM that includes the loaded capture), the tempo, and
/// the tuner. What it can never hold is the rig's structure: adding, removing
/// or reordering blocks belongs to rigs, not scenes.
class SnapshotStrip::AddPanel final : public juce::Component
{
public:
    /// `editIndex` >= 0 edits an existing snapshot's name and safes instead of
    /// creating one, so a scene can be re-scoped without rebuilding it.
    AddPanel(BlockRigProcessor& processor, std::function<void()> onSaved, int editIndex = -1)
        : mProcessor(processor)
        , mOnSaved(std::move(onSaved))
        , mEditIndex(editIndex)
    {
        mNameLabel.setText("NAME", juce::dontSendNotification);
        mNameLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        mNameLabel.setColour(juce::Label::textColourId, theme::colours::textFaint);
        addAndMakeVisible(mNameLabel);

        const auto& existing = mProcessor.getSnapshots().getSnapshots();
        const bool editing = mEditIndex >= 0 && mEditIndex < static_cast<int>(existing.size());

        mName.setText(editing ? existing[static_cast<size_t>(mEditIndex)].name
                              : "Snapshot " + juce::String(static_cast<int>(existing.size() + 1)));
        mName.setSelectAllWhenFocused(true);
        addAndMakeVisible(mName);

        mSafesLabel.setText("SAVED IN THIS SNAPSHOT", juce::dontSendNotification);
        mSafesLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        mSafesLabel.setColour(juce::Label::textColourId, theme::colours::textFaint);
        addAndMakeVisible(mSafesLabel);

        // Everything defaults on for a new snapshot; editing shows what this one
        // actually holds.
        for (auto* block : mProcessor.getChain().getBlocks())
        {
            const bool ticked =
                !editing
                || existing[static_cast<size_t>(mEditIndex)].blockStates.count(block->getUid()) > 0;

            auto toggle = std::make_unique<juce::ToggleButton>(block->getDisplayName());
            toggle->setToggleState(ticked, juce::dontSendNotification);
            toggle->getProperties().set("uid", block->getUid());
            mHolder.addAndMakeVisible(*toggle);
            mBlockToggles.push_back(std::move(toggle));
        }

        mTempoToggle.setToggleState(!editing || existing[static_cast<size_t>(mEditIndex)].includeTempo,
                                    juce::dontSendNotification);
        mHolder.addAndMakeVisible(mTempoToggle);
        mTunerToggle.setToggleState(!editing || existing[static_cast<size_t>(mEditIndex)].includeTuner,
                                    juce::dontSendNotification);
        mHolder.addAndMakeVisible(mTunerToggle);

        mSave.setButtonText(editing ? "Update snapshot" : "Save snapshot");

        mViewport.setViewedComponent(&mHolder, false);
        mViewport.setScrollBarsShown(true, false);
        addAndMakeVisible(mViewport);

        mSave.onClick = [this] { save(); };
        addAndMakeVisible(mSave);

        setSize(360, 200 + juce::jmin(5, static_cast<int>(mBlockToggles.size())) * 26);
    }

    void paint(juce::Graphics& g) override { g.fillAll(theme::colours::background); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(theme::metrics::gap);

        mNameLabel.setBounds(area.removeFromTop(14));
        mName.setBounds(area.removeFromTop(28));
        area.removeFromTop(10);
        mSafesLabel.setBounds(area.removeFromTop(14));

        auto buttons = area.removeFromBottom(34);
        mSave.setBounds(buttons.removeFromRight(120).reduced(0, 3));

        area.removeFromBottom(4);
        mViewport.setBounds(area);

        const int rowHeight = 26;
        mHolder.setSize(mViewport.getMaximumVisibleWidth(),
                        rowHeight * (static_cast<int>(mBlockToggles.size()) + 2));

        int y = 0;
        for (auto& toggle : mBlockToggles)
        {
            toggle->setBounds(0, y, mHolder.getWidth(), rowHeight);
            y += rowHeight;
        }
        mTempoToggle.setBounds(0, y, mHolder.getWidth(), rowHeight);
        mTunerToggle.setBounds(0, y + rowHeight, mHolder.getWidth(), rowHeight);
    }

private:
    void save()
    {
        juce::StringArray uids;
        for (const auto& toggle : mBlockToggles)
            if (toggle->getToggleState())
                uids.add(toggle->getProperties()["uid"].toString());

        auto name = mName.getText().trim();
        if (name.isEmpty())
            name = "Snapshot";

        auto& bank = mProcessor.getSnapshots();
        auto captured = snapshots::Bank::capture(mProcessor, name, uids,
                                                 mTempoToggle.getToggleState(),
                                                 mTunerToggle.getToggleState());

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

        // Hosted inside a BlockWindow; its close callback tears us down.
        if (auto* window = findParentComponentOfClass<BlockWindow>())
            if (window->onClose)
                window->onClose();
    }

    BlockRigProcessor& mProcessor;
    std::function<void()> mOnSaved;

    juce::Label mNameLabel, mSafesLabel;
    juce::TextEditor mName;
    juce::Viewport mViewport;
    juce::Component mHolder;
    std::vector<std::unique_ptr<juce::ToggleButton>> mBlockToggles;
    juce::ToggleButton mTempoToggle{"Tempo & time signature"};
    juce::ToggleButton mTunerToggle{"Tuner"};
    juce::TextButton mSave{"Save snapshot"};
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
                openPanel(std::move(panel), "Edit snapshot", width, height);
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
    openPanel(std::move(panel), "New snapshot", width, height);
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
        g.drawText("none yet — press  +  to save the current settings as a scene",
                   getLocalBounds().withTrimmedLeft(120), juce::Justification::centredLeft, false);
    }
}

void SnapshotStrip::resized()
{
    auto area = getLocalBounds().reduced(theme::metrics::padding, 5);
    area.removeFromLeft(66); // the SNAPSHOTS caption

    mEditToggle.setBounds(area.removeFromRight(56).reduced(0, 2));
    area.removeFromRight(6);

    mAddButton.setBounds(area.removeFromLeft(30).reduced(0, 2));
    area.removeFromLeft(6);

    // Pads share the row equally, like the design's flex:1.
    if (!mChips.empty())
    {
        const auto pad = juce::jmax(88, (area.getWidth() - 6 * static_cast<int>(mChips.size()))
                                            / static_cast<int>(mChips.size()));

        for (auto& chip : mChips)
        {
            chip->setBounds(area.removeFromLeft(juce::jmin(pad, 220)));
            area.removeFromLeft(6);
        }
    }
}

} // namespace blockrig
