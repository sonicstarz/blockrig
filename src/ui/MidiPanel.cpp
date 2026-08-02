#include "ui/MidiPanel.h"

#include "ui/Theme.h"

namespace blockrig
{
namespace
{
struct GlobalTarget
{
    const char* id;
    const char* label;
    bool toggle;
};

/// The actions worth a footswitch. Continuous pedals map to block parameters.
constexpr GlobalTarget kGlobalTargets[] = {
    {"mute", "Mute", true},
    {"tuner", "Tuner", true},
    {"tap", "Tap tempo", true},
    {"metronome", "Metronome", true},
    {"snapshotNext", "Next snapshot", true},
    {"snapshotPrev", "Previous snapshot", true},
    {"rigNext", "Next rig", true},
    {"rigPrev", "Previous rig", true},
};

/// Plug-ins can expose hundreds of parameters; the menu stays usable by
/// capping what it lists. Rare deep parameters can still be mapped by mapping
/// a neighbour and editing the rig file, which nobody will ever do - the first
/// hundred covers every knob a pedalboard cares about.
constexpr int kMaxListedParameters = 100;
} // namespace

class MidiPanel::Model final : public juce::ListBoxModel
{
public:
    explicit Model(MidiPanel& owner)
        : mOwner(owner)
    {
    }

    int getNumRows() override { return static_cast<int>(mOwner.mMappings.size()); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool hovered) override
    {
        if (row >= static_cast<int>(mOwner.mMappings.size()))
            return;

        const auto& mapping = mOwner.mMappings[static_cast<size_t>(row)];
        const bool armed = mOwner.mProcessor.getMidiEngine().getArmedIndex() == row;
        auto area = juce::Rectangle<int>(0, 0, width, height).reduced(2, 2).toFloat();

        g.setColour(hovered ? theme::colours::panelRaised : theme::colours::panel);
        g.fillRoundedRectangle(area, theme::metrics::smallCornerRadius);

        if (armed)
        {
            g.setColour(theme::colours::warn);
            g.drawRoundedRectangle(area, theme::metrics::smallCornerRadius, 1.4f);
        }

        // Left: what it steers. Right: what steers it.
        g.setColour(theme::colours::text);
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(mapping.description, area.reduced(10.0f, 0.0f).withTrimmedRight(90.0f),
                   juce::Justification::centredLeft, true);

        g.setColour(armed ? theme::colours::warn
                          : (mapping.cc >= 0 ? theme::colours::accent : theme::colours::textFaint));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        auto trigger = armed ? juce::String("move it...")
                             : (mapping.cc >= 0 ? "CC " + juce::String(mapping.cc) : "unmapped");

        if (!armed && mapping.cc >= 0 && mapping.channel > 0)
            trigger += " ch" + juce::String(mapping.channel);

        g.drawText(trigger, area.removeFromRight(86.0f), juce::Justification::centred, false);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& event) override
    {
        mOwner.rowClicked(row, event.mods.isPopupMenu());
    }

private:
    MidiPanel& mOwner;
};

//==============================================================================
MidiPanel::MidiPanel(BlockRigProcessor& processor)
    : mProcessor(processor)
{
    mAddButton.onClick = [this] { showAddMenu(); };
    addAndMakeVisible(mAddButton);

    mHint.setText("Add a mapping, then move a knob or press a switch on your controller. "
                  "Right-click a row to re-learn or remove it.",
                  juce::dontSendNotification);
    mHint.setFont(juce::FontOptions(11.5f));
    mHint.setColour(juce::Label::textColourId, theme::colours::textFaint);
    addAndMakeVisible(mHint);

    mModel = std::make_unique<Model>(*this);
    mList.setModel(mModel.get());
    mList.setRowHeight(36);
    mList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(mList);

    mOptionsButton.setTooltip("Clock sync and what program changes select.");
    mOptionsButton.onClick = [this] { showOptionsMenu(-1); };
    addAndMakeVisible(mOptionsButton);

    mActivity.setFont(juce::FontOptions(11.0f));
    mActivity.setColour(juce::Label::textColourId, theme::colours::textFaint);
    mActivity.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(mActivity);

    mProcessor.getMidiEngine().onMappingsChanged = [this] { refresh(); };
    refresh();
    startTimerHz(10);
}

void MidiPanel::timerCallback()
{
    auto& engine = mProcessor.getMidiEngine();
    const auto activity = engine.getLastActivity();
    const auto age = juce::Time::currentTimeMillis() - activity.timeMs;

    juce::String text;

    if (activity.cc >= 0 && age < 4000)
        text = "CC " + juce::String(activity.cc) + " = " + juce::String(activity.value) + "  (ch "
               + juce::String(activity.channel) + ")";
    else
        text = "no MIDI yet";

    if (engine.isReceivingClock())
        text += juce::String::fromUTF8("   ·   clock in");

    mActivity.setText(text, juce::dontSendNotification);
    mList.repaint(); // armed rows animate their prompt
}

void MidiPanel::showOptionsMenu(int row)
{
    auto& engine = mProcessor.getMidiEngine();
    juce::PopupMenu menu;

    if (row < 0)
    {
        menu.addSectionHeader("Program change selects");
        menu.addItem(1, "Snapshots", true,
                     engine.getProgramTarget() == MidiEngine::ProgramTarget::snapshots);
        menu.addItem(2, "Rigs", true, engine.getProgramTarget() == MidiEngine::ProgramTarget::rigs);
        menu.addSeparator();
        menu.addItem(3, "Follow MIDI clock", true, engine.getFollowMidiClock());

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&mOptionsButton),
                           [this, &engine](int choice) {
            if (choice == 1)
                engine.setProgramTarget(MidiEngine::ProgramTarget::snapshots);
            else if (choice == 2)
                engine.setProgramTarget(MidiEngine::ProgramTarget::rigs);
            else if (choice == 3)
                engine.setFollowMidiClock(!engine.getFollowMidiClock());
        });
        return;
    }

    // Per-mapping: which channel, and how far the pedal's travel reaches.
    const auto mappings = engine.getMappings();
    if (row >= static_cast<int>(mappings.size()))
        return;

    const auto& mapping = mappings[static_cast<size_t>(row)];

    menu.addSectionHeader("Channel");
    menu.addItem(200, "Any", true, mapping.channel == 0);
    for (int channel = 1; channel <= 16; ++channel)
        menu.addItem(200 + channel, juce::String(channel), true, mapping.channel == channel);

    if (mapping.globalId.isEmpty())
    {
        menu.addSectionHeader("Pedal range");
        menu.addItem(300, "Full (0 - 100%)", true, mapping.minimum == 0.0f && mapping.maximum == 1.0f);
        menu.addItem(301, "Lower half (0 - 50%)", true, mapping.maximum == 0.5f);
        menu.addItem(302, "Upper half (50 - 100%)", true, mapping.minimum == 0.5f);
        menu.addItem(303, "Inverted (100 - 0%)", true, mapping.minimum > mapping.maximum);
    }

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, row](int choice) {
        auto& innerEngine = mProcessor.getMidiEngine();
        auto mappings = innerEngine.getMappings();

        if (row >= static_cast<int>(mappings.size()) || choice == 0)
            return;

        auto& target = mappings[static_cast<size_t>(row)];

        if (choice >= 200 && choice <= 216)
            target.channel = choice - 200;
        else if (choice == 300) { target.minimum = 0.0f; target.maximum = 1.0f; }
        else if (choice == 301) { target.minimum = 0.0f; target.maximum = 0.5f; }
        else if (choice == 302) { target.minimum = 0.5f; target.maximum = 1.0f; }
        else if (choice == 303) { target.minimum = 1.0f; target.maximum = 0.0f; }

        innerEngine.setMappings(std::move(mappings));
    });
}

MidiPanel::~MidiPanel()
{
    stopTimer();
    mProcessor.getMidiEngine().onMappingsChanged = nullptr;
}

void MidiPanel::refresh()
{
    mMappings = mProcessor.getMidiEngine().getMappings();
    mList.updateContent();
    mList.repaint();
}

void MidiPanel::showAddMenu()
{
    juce::PopupMenu menu;

    menu.addSectionHeader("Global");
    for (int i = 0; i < static_cast<int>(std::size(kGlobalTargets)); ++i)
        menu.addItem(1000 + i, kGlobalTargets[i].label);

    // Every block, every parameter (capped). The submenu is the whole learnable
    // surface of the rig.
    const auto blocks = mProcessor.getChain().getBlocks();

    for (int blockIndex = 0; blockIndex < static_cast<int>(blocks.size()); ++blockIndex)
    {
        auto* plugin = blocks[static_cast<size_t>(blockIndex)]->getPlugin();
        if (plugin == nullptr)
            continue;

        juce::PopupMenu parameterMenu;
        const auto& parameters = plugin->getParameters();
        const auto listed = juce::jmin(parameters.size(), kMaxListedParameters);

        for (int p = 0; p < listed; ++p)
            parameterMenu.addItem(10000 + blockIndex * 1000 + p, parameters[p]->getName(48));

        if (parameters.size() > kMaxListedParameters)
            parameterMenu.addItem(juce::PopupMenu::Item(juce::String(parameters.size() - kMaxListedParameters)
                                                        + " more not listed")
                                      .setEnabled(false));

        menu.addSubMenu(blocks[static_cast<size_t>(blockIndex)]->getDisplayName(), parameterMenu);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&mAddButton), [this](int choice) {
        if (choice == 0)
            return;

        MidiEngine::Mapping mapping;

        if (choice >= 10000)
        {
            const auto blockIndex = (choice - 10000) / 1000;
            const auto parameterIndex = (choice - 10000) % 1000;
            const auto blocks = mProcessor.getChain().getBlocks();

            if (blockIndex >= static_cast<int>(blocks.size()))
                return;

            auto* block = blocks[static_cast<size_t>(blockIndex)];
            auto* plugin = block->getPlugin();
            if (plugin == nullptr || parameterIndex >= plugin->getParameters().size())
                return;

            mapping.blockUid = block->getUid();
            mapping.parameterIndex = parameterIndex;
            mapping.description = block->getDisplayName() + juce::String::fromUTF8("  ·  ")
                                  + plugin->getParameters()[parameterIndex]->getName(48);
        }
        else if (choice >= 1000)
        {
            const auto& target = kGlobalTargets[choice - 1000];
            mapping.globalId = target.id;
            mapping.toggle = target.toggle;
            mapping.description = target.label;
        }

        mProcessor.getMidiEngine().addMapping(std::move(mapping));
    });
}

void MidiPanel::rowClicked(int row, bool isPopup)
{
    if (row < 0 || row >= static_cast<int>(mMappings.size()))
        return;

    if (!isPopup)
    {
        // Plain click re-arms: tap the row, wiggle the controller.
        mProcessor.getMidiEngine().armLearn(row);
        mList.repaint();
        return;
    }

    juce::PopupMenu menu;
    menu.addItem(1, "Learn (move a controller)");
    menu.addItem(3, "Channel and range...");
    menu.addItem(2, "Remove");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, row](int choice) {
        if (choice == 1)
        {
            mProcessor.getMidiEngine().armLearn(row);
            mList.repaint();
        }
        else if (choice == 2)
        {
            mProcessor.getMidiEngine().removeMapping(row);
        }
        else if (choice == 3)
        {
            showOptionsMenu(row);
        }
    });
}

void MidiPanel::paint(juce::Graphics& g)
{
    g.fillAll(theme::colours::background);
}

void MidiPanel::resized()
{
    auto area = getLocalBounds().reduced(theme::metrics::gap);

    auto top = area.removeFromTop(30);
    mAddButton.setBounds(top.removeFromLeft(130));
    top.removeFromLeft(6);
    mOptionsButton.setBounds(top.removeFromLeft(84));
    top.removeFromLeft(theme::metrics::gap);
    mHint.setBounds(top);

    mActivity.setBounds(area.removeFromBottom(18));
    area.removeFromTop(6);
    mList.setBounds(area);
}

} // namespace blockrig
