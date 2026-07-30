#include "ui/BlockPicker.h"

#include <algorithm>

#include "host/PluginCatalog.h"
#include "ui/Theme.h"

namespace blockrig
{
namespace
{
constexpr int kRowHeight = 26;
constexpr int kMaxRecents = 6;
constexpr int kPickerWidth = 380;
constexpr int kPickerHeight = 420;
} // namespace

juce::Array<juce::PluginDescription>& BlockPicker::getRecents()
{
    static juce::Array<juce::PluginDescription> recents;
    return recents;
}

BlockPicker::BlockPicker(PluginCatalog& catalog)
    : mCatalog(catalog)
{
    mSearch.setTextToShowWhenEmpty("Search blocks...", theme::colours::textFaint);
    mSearch.addListener(this);
    mSearch.setFont(juce::FontOptions(14.0f));
    addAndMakeVisible(mSearch);

    mList.setRowHeight(kRowHeight);
    mList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    mList.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(mList);

    mHint.setFont(juce::FontOptions(11.0f));
    mHint.setColour(juce::Label::textColourId, theme::colours::textFaint);
    mHint.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(mHint);

    rebuildEntries();
    setSize(kPickerWidth, kPickerHeight);
}

BlockPicker::~BlockPicker()
{
    mSearch.removeListener(this);
    mList.setModel(nullptr);
}

void BlockPicker::show(PluginCatalog& catalog, juce::Component& target,
                       std::function<void(const juce::PluginDescription&)> onChosen)
{
    auto picker = std::make_unique<BlockPicker>(catalog);
    auto* pickerPtr = picker.get();

    auto& callout = juce::CallOutBox::launchAsynchronously(std::move(picker),
                                                           target.getScreenBounds(), nullptr);

    pickerPtr->onChosen = [onChosen, &callout](const juce::PluginDescription& description) {
        if (onChosen)
            onChosen(description);
        callout.dismiss();
    };

    pickerPtr->onDismiss = [&callout] { callout.dismiss(); };
    pickerPtr->mSearch.grabKeyboardFocus();
}

void BlockPicker::rebuildEntries()
{
    mEntries.clear();

    const auto query = mSearch.getText().trim();

    const auto matches = [&query](const juce::PluginDescription& description) {
        if (query.isEmpty())
            return true;
        return description.name.containsIgnoreCase(query)
               || description.manufacturerName.containsIgnoreCase(query)
               || description.category.containsIgnoreCase(query);
    };

    const auto addSection = [this](const juce::String& label) {
        Entry header;
        header.isHeader = true;
        header.sectionLabel = label;
        mEntries.push_back(std::move(header));
    };

    // Built-ins first: they are the reason this app exists.
    juce::Array<juce::PluginDescription> builtIns;
    for (const auto& description : mCatalog.getBuiltInDescriptions())
        if (matches(description))
            builtIns.add(description);

    if (!builtIns.isEmpty())
    {
        addSection("Built-in");
        for (const auto& description : builtIns)
        {
            Entry entry;
            entry.description = description;
            entry.isBuiltIn = true;
            mEntries.push_back(std::move(entry));
        }
    }

    // Then whatever the user reached for last.
    juce::Array<juce::PluginDescription> recents;
    for (const auto& description : getRecents())
        if (matches(description))
            recents.add(description);

    if (!recents.isEmpty())
    {
        addSection("Recent");
        for (const auto& description : recents)
        {
            Entry entry;
            entry.description = description;
            entry.isRecent = true;
            mEntries.push_back(std::move(entry));
        }
    }

    // Then everything installed, alphabetically. Effects only: an instrument in
    // a guitar chain has nothing to process.
    const auto types = mCatalog.getKnownPluginList().getTypes();
    std::vector<juce::PluginDescription> installed(types.begin(), types.end());
    std::sort(installed.begin(), installed.end(),
              [](const juce::PluginDescription& a, const juce::PluginDescription& b) {
                  return a.name.compareIgnoreCase(b.name) < 0;
              });

    bool addedHeader = false;
    for (const auto& description : installed)
    {
        if (description.isInstrument || !matches(description))
            continue;

        if (!addedHeader)
        {
            addSection(query.isEmpty() ? "All plug-ins" : "Matches");
            addedHeader = true;
        }

        Entry entry;
        entry.description = description;
        mEntries.push_back(std::move(entry));
    }

    const int selectable = static_cast<int>(std::count_if(mEntries.begin(), mEntries.end(),
                                                          [](const Entry& e) { return !e.isHeader; }));
    mHint.setText(juce::String(selectable) + " block" + (selectable == 1 ? "" : "s")
                      + "   •   Enter to add, Esc to dismiss",
                  juce::dontSendNotification);

    mList.updateContent();

    // Preselect the first real row so Enter does the obvious thing.
    for (int row = 0; row < static_cast<int>(mEntries.size()); ++row)
    {
        if (!mEntries[static_cast<size_t>(row)].isHeader)
        {
            mList.selectRow(row);
            break;
        }
    }
}

void BlockPicker::resized()
{
    auto area = getLocalBounds().reduced(theme::metrics::gap);
    mSearch.setBounds(area.removeFromTop(30));
    area.removeFromTop(6);
    mHint.setBounds(area.removeFromBottom(16));
    area.removeFromBottom(4);
    mList.setBounds(area);
}

void BlockPicker::paint(juce::Graphics& g)
{
    g.fillAll(theme::colours::panel);
}

void BlockPicker::textEditorTextChanged(juce::TextEditor&)
{
    rebuildEntries();
}

void BlockPicker::textEditorReturnKeyPressed(juce::TextEditor&)
{
    chooseRow(mList.getSelectedRow());
}

void BlockPicker::textEditorEscapeKeyPressed(juce::TextEditor&)
{
    if (onDismiss)
        onDismiss();
}

int BlockPicker::getNumRows()
{
    return static_cast<int>(mEntries.size());
}

void BlockPicker::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (!juce::isPositiveAndBelow(row, static_cast<int>(mEntries.size())))
        return;

    const auto& entry = mEntries[static_cast<size_t>(row)];
    const juce::Rectangle<int> bounds{0, 0, width, height};

    if (entry.isHeader)
    {
        g.setColour(theme::colours::textFaint);
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText(entry.sectionLabel.toUpperCase(), bounds.reduced(6, 0).withTrimmedTop(6),
                   juce::Justification::bottomLeft, true);
        return;
    }

    if (rowIsSelected)
    {
        g.setColour(theme::colours::accentDim);
        g.fillRoundedRectangle(bounds.reduced(2, 1).toFloat(), theme::metrics::smallCornerRadius);
    }

    auto text = bounds.reduced(8, 0);

    // Format badge on the right, so scanning the list by type is easy.
    const auto badge = entry.isBuiltIn ? juce::String("BUILT-IN")
                                       : entry.description.pluginFormatName.toUpperCase();
    g.setColour(entry.isBuiltIn ? theme::colours::accent : theme::colours::textFaint);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText(badge, text.removeFromRight(56), juce::Justification::centredRight, false);

    g.setColour(theme::colours::text);
    g.setFont(juce::FontOptions(13.0f));
    const auto nameWidth = juce::jmin(text.getWidth(), 190);
    g.drawText(entry.description.name, text.removeFromLeft(nameWidth), juce::Justification::centredLeft, true);

    g.setColour(theme::colours::textFaint);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(entry.description.manufacturerName, text, juce::Justification::centredLeft, true);
}

void BlockPicker::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    // Selection only. Handling double clicks here as well as in
    // listBoxItemDoubleClicked meant both fired for one double click, and the
    // plug-in was added twice.
    if (juce::isPositiveAndBelow(row, static_cast<int>(mEntries.size()))
        && mEntries[static_cast<size_t>(row)].isHeader)
        mList.deselectAllRows();
}

void BlockPicker::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    chooseRow(row);
}

void BlockPicker::returnKeyPressed(int lastRowSelected)
{
    chooseRow(lastRowSelected);
}

void BlockPicker::chooseRow(int row)
{
    if (!juce::isPositiveAndBelow(row, static_cast<int>(mEntries.size())))
        return;

    const auto& entry = mEntries[static_cast<size_t>(row)];
    if (entry.isHeader)
        return;

    // Keep the recents list short and free of duplicates.
    auto& recents = getRecents();
    for (int i = recents.size(); --i >= 0;)
        if (recents[i].fileOrIdentifier == entry.description.fileOrIdentifier)
            recents.remove(i);

    recents.insert(0, entry.description);
    while (recents.size() > kMaxRecents)
        recents.remove(recents.size() - 1);

    if (onChosen)
        onChosen(entry.description);
}

} // namespace blockrig
