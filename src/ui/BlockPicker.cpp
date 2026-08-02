#include "ui/BlockPicker.h"

#include <algorithm>

#include "host/PluginCatalog.h"
#include "ui/BlockCategories.h"
#include "ui/Theme.h"

namespace blockrig
{
namespace
{
constexpr int kRowHeight = 52;
constexpr int kMaxRecents = 6;
constexpr int kPickerWidth = 560;
constexpr int kPickerHeight = 540;
constexpr int kStarZone = 52; ///< right-edge strip a row click reads as the star

/// The chips use the short names people say, not the full category names.
juce::String shortCategoryName(BlockCategory category)
{
    switch (category)
    {
        case BlockCategory::cabinet: return "Cab";
        case BlockCategory::modulation: return "Mod";
        case BlockCategory::dynamics: return "Dyn";
        default: break;
    }

    return getCategoryName(category);
}

juce::String formatBadge(const juce::PluginDescription& description, bool isBuiltIn)
{
    if (isBuiltIn)
        return "Built-in";
    return description.pluginFormatName == "AudioUnit" ? juce::String("AU")
                                                       : description.pluginFormatName;
}

void drawStar(juce::Graphics& g, juce::Rectangle<float> bounds, bool starred)
{
    juce::Path star;
    star.addStar(bounds.getCentre(), 5, bounds.getWidth() * 0.22f, bounds.getWidth() * 0.5f,
                 0.0f);
    g.setColour(starred ? theme::colours::accent : theme::colours::outlineStrong);
    g.fillPath(star);
}
} // namespace

//==============================================================================
int BlockPicker::ChipRow::getPreferredHeight(int width)
{
    layoutChips(width);
    return mChips.empty() ? 0 : static_cast<int>(mChips.back().bounds.getBottom()) + 4;
}

void BlockPicker::ChipRow::layoutChips(int width)
{
    if (mLayoutWidth == width && !mChips.empty())
        return;

    mLayoutWidth = width;
    mChips.clear();

    const auto font = theme::fonts::ui(13.0f, 500);
    constexpr float chipHeight = 30.0f;
    constexpr float gap = 6.0f;
    float x = 0.0f, y = 0.0f;

    const auto add = [&](int category, const juce::String& label) {
        const auto chipWidth =
            juce::GlyphArrangement::getStringWidth(font, label) + 24.0f;

        if (x + chipWidth > static_cast<float>(width) && x > 0.0f)
        {
            x = 0.0f;
            y += chipHeight + gap;
        }

        mChips.push_back({category, label, {x, y, chipWidth, chipHeight}});
        x += chipWidth + gap;
    };

    add(-1, "All");
    const auto categories = getAllCategories();
    for (int i = 0; i < categories.size(); ++i)
        if (categories[i] != BlockCategory::other)
            add(i, shortCategoryName(categories[i]));
}

void BlockPicker::ChipRow::paint(juce::Graphics& g)
{
    layoutChips(getWidth());
    const auto categories = getAllCategories();

    for (int i = 0; i < static_cast<int>(mChips.size()); ++i)
    {
        const auto& chip = mChips[static_cast<size_t>(i)];
        const bool selected = chip.category == mSelected;
        const auto radius = chip.bounds.getHeight() * 0.5f;

        if (chip.category < 0)
        {
            // "All": neutral fill when selected, outline otherwise.
            if (selected)
            {
                g.setColour(juce::Colour(0xff242030));
                g.fillRoundedRectangle(chip.bounds, radius);
                g.setColour(theme::colours::text);
            }
            else
            {
                g.setColour(theme::colours::outline);
                g.drawRoundedRectangle(chip.bounds.reduced(0.75f), radius, 1.5f);
                g.setColour(i == mHover ? theme::colours::text : theme::colours::textFaint);
            }
        }
        else
        {
            const auto colour = getCategoryColour(categories[chip.category]);

            if (selected)
            {
                g.setColour(colour.withAlpha(0.14f));
                g.fillRoundedRectangle(chip.bounds, radius);
                g.setColour(colour);
                g.drawRoundedRectangle(chip.bounds.reduced(0.75f), radius, 1.5f);
            }
            else
            {
                g.setColour(colour.withAlpha(i == mHover ? 0.7f : 0.4f));
                g.drawRoundedRectangle(chip.bounds.reduced(0.75f), radius, 1.5f);
            }

            g.setColour(colour);
        }

        g.setFont(theme::fonts::ui(13.0f, 600));
        g.drawText(chip.label, chip.bounds, juce::Justification::centred, false);
    }
}

void BlockPicker::ChipRow::mouseDown(const juce::MouseEvent& event)
{
    for (const auto& chip : mChips)
        if (chip.bounds.contains(event.position))
        {
            if (mSelected != chip.category)
            {
                mSelected = chip.category;
                repaint();
                if (onChanged)
                    onChanged();
            }
            return;
        }
}

void BlockPicker::ChipRow::mouseMove(const juce::MouseEvent& event)
{
    int hover = -1;
    for (int i = 0; i < static_cast<int>(mChips.size()); ++i)
        if (mChips[static_cast<size_t>(i)].bounds.contains(event.position))
            hover = i;

    if (hover != mHover)
    {
        mHover = hover;
        repaint();
    }
}

void BlockPicker::ChipRow::mouseExit(const juce::MouseEvent&)
{
    mHover = -1;
    repaint();
}

//==============================================================================
juce::Array<juce::PluginDescription>& BlockPicker::getRecents()
{
    static juce::Array<juce::PluginDescription> recents;
    return recents;
}

juce::File BlockPicker::getStarsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support")
        .getChildFile("BlockRig")
        .getChildFile("starred-blocks.txt");
}

juce::StringArray& BlockPicker::getStars()
{
    static juce::StringArray stars = [] {
        juce::StringArray loaded;
        loaded.addLines(getStarsFile().loadFileAsString());
        loaded.removeEmptyStrings();
        return loaded;
    }();

    return stars;
}

bool BlockPicker::isStarred(const juce::PluginDescription& description)
{
    return getStars().contains(description.createIdentifierString());
}

void BlockPicker::toggleStar(const juce::PluginDescription& description)
{
    auto& stars = getStars();
    const auto id = description.createIdentifierString();

    if (stars.contains(id))
        stars.removeString(id);
    else
        stars.add(id);

    getStarsFile().getParentDirectory().createDirectory();
    getStarsFile().replaceWithText(stars.joinIntoString("\n"));
}

//==============================================================================
BlockPicker::BlockPicker(PluginCatalog& catalog)
    : mCatalog(catalog)
{
    mSearch.setTextToShowWhenEmpty("Search blocks...", theme::colours::textFaint);
    mSearch.addListener(this);
    mSearch.setFont(theme::fonts::ui(15.0f));
    addAndMakeVisible(mSearch);

    mChips.onChanged = [this] { rebuildEntries(); };
    addAndMakeVisible(mChips);

    mList.setRowHeight(kRowHeight);
    mList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    mList.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(mList);

    // Everything selectable in the catalog, for the "N matches of M" footer.
    mTotalBlocks = mCatalog.getBuiltInDescriptions().size();
    for (const auto& type : mCatalog.getKnownPluginList().getTypes())
        if (!type.isInstrument)
            ++mTotalBlocks;

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
    const auto categories = getAllCategories();
    const auto chipCategory =
        mChips.getSelected() >= 0 ? categories[mChips.getSelected()] : BlockCategory::other;
    const bool filtered = mChips.getSelected() >= 0;

    const auto matches = [&](const juce::PluginDescription& description, BlockCategory category) {
        if (filtered && category != chipCategory)
            return false;
        if (query.isEmpty())
            return true;
        return description.name.containsIgnoreCase(query)
               || description.manufacturerName.containsIgnoreCase(query)
               || description.category.containsIgnoreCase(query);
    };

    const auto addSection = [this](const juce::String& label,
                                   BlockCategory category = BlockCategory::other) {
        Entry header;
        header.isHeader = true;
        header.sectionLabel = label;
        header.category = category;
        mEntries.push_back(std::move(header));
    };

    // Built-ins first: they are the reason this app exists.
    juce::Array<juce::PluginDescription> builtIns;
    for (const auto& description : mCatalog.getBuiltInDescriptions())
        if (matches(description, categoriseBlock(description)))
            builtIns.add(description);

    if (!builtIns.isEmpty())
    {
        if (!filtered)
            addSection("Built-in");
        for (const auto& description : builtIns)
        {
            Entry entry;
            entry.description = description;
            entry.isBuiltIn = true;
            entry.category = categoriseBlock(description);
            mEntries.push_back(std::move(entry));
        }
    }

    const auto isListedBuiltIn = [&builtIns](const juce::PluginDescription& description) {
        for (const auto& b : builtIns)
            if (b.createIdentifierString() == description.createIdentifierString())
                return true;
        return false;
    };

    // Then the starred blocks, then whatever the user reached for last.
    if (!filtered)
    {
        const auto types = mCatalog.getKnownPluginList().getTypes();

        juce::Array<juce::PluginDescription> starred;
        for (const auto& description : types)
            if (!description.isInstrument && isStarred(description)
                && matches(description, categoriseBlock(description))
                && !isListedBuiltIn(description))
                starred.add(description);

        if (!starred.isEmpty())
        {
            addSection("Starred");
            for (const auto& description : starred)
            {
                Entry entry;
                entry.description = description;
                entry.category = categoriseBlock(description);
                mEntries.push_back(std::move(entry));
            }
        }

        juce::Array<juce::PluginDescription> recents;
        for (const auto& description : getRecents())
            if (matches(description, categoriseBlock(description)))
                recents.add(description);

        if (!recents.isEmpty())
        {
            addSection("Recent");
            for (const auto& description : recents)
            {
                Entry entry;
                entry.description = description;
                entry.isRecent = true;
                entry.category = categoriseBlock(description);
                mEntries.push_back(std::move(entry));
            }
        }
    }

    // Then everything installed, grouped by category in signal-chain order. A
    // flat A-Z list of 800 plug-ins is a wall; grouping makes it browsable.
    // With a category chip active the group headers would be redundant.
    const auto types = mCatalog.getKnownPluginList().getTypes();
    std::vector<juce::PluginDescription> installed(types.begin(), types.end());
    std::sort(installed.begin(), installed.end(),
              [](const juce::PluginDescription& a, const juce::PluginDescription& b) {
                  return a.name.compareIgnoreCase(b.name) < 0;
              });

    for (const auto category : categories)
    {
        bool addedHeader = false;

        for (const auto& description : installed)
        {
            if (description.isInstrument)
                continue;

            if (categoriseBlock(description) != category || !matches(description, category))
                continue;

            if (!addedHeader && !filtered)
            {
                addSection(getCategoryName(category), category);
                addedHeader = true;
            }

            Entry entry;
            entry.description = description;
            entry.category = category;
            mEntries.push_back(std::move(entry));
        }
    }

    const int selectable = static_cast<int>(std::count_if(mEntries.begin(), mEntries.end(),
                                                          [](const Entry& e) { return !e.isHeader; }));
    mFooterLeft = juce::String(selectable) + (selectable == 1 ? " match of " : " matches of ")
                  + juce::String(mTotalBlocks);

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

    repaint();
}

void BlockPicker::resized()
{
    auto area = getLocalBounds().reduced(theme::metrics::gap);
    mSearch.setBounds(area.removeFromTop(40));
    area.removeFromTop(10);
    mChips.setBounds(area.removeFromTop(mChips.getPreferredHeight(area.getWidth())));
    area.removeFromTop(8);
    area.removeFromBottom(20); // footer, painted
    mList.setBounds(area);
}

void BlockPicker::paint(juce::Graphics& g)
{
    g.fillAll(theme::colours::panel);

    auto footer = getLocalBounds().reduced(theme::metrics::gap).removeFromBottom(16);

    g.setColour(theme::colours::textGhost);
    g.setFont(theme::fonts::ui(11.0f));
    g.drawText(mFooterLeft, footer, juce::Justification::centredLeft, false);
    g.drawText(juce::String::fromUTF8("Enter to add  \xc2\xb7  Esc to close"), footer,
               juce::Justification::centredRight, false);
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
        auto header = bounds.reduced(6, 0).withTrimmedTop(height / 2 - 4);

        if (entry.category != BlockCategory::other)
            drawCategoryIcon(g, header.removeFromLeft(16).toFloat().reduced(1.0f), entry.category,
                             getCategoryColour(entry.category), 1.3f);

        g.setColour(theme::colours::textFaint);
        g.setFont(theme::fonts::ui(11.0f, 500));
        g.drawText(entry.sectionLabel, header.withTrimmedLeft(4),
                   juce::Justification::centredLeft, true);
        return;
    }

    const auto colour = getCategoryColour(entry.category);

    // Highlighted row: amber wash with the 3px bar on the left edge.
    if (rowIsSelected)
    {
        g.setColour(theme::colours::accent.withAlpha(0.1f));
        g.fillRect(bounds);
        g.setColour(theme::colours::accent);
        g.fillRect(0, 0, 3, height);
    }

    auto text = bounds.reduced(10, 0);

    // The 38px icon chip: category tint, border, and the same glyph the block
    // will carry once it is in the lane.
    const auto chip = text.removeFromLeft(44).withSizeKeepingCentre(38, 38).toFloat();
    g.setColour(colour.withAlpha(0.12f));
    g.fillRoundedRectangle(chip, 10.0f);
    g.setColour(colour.withAlpha(0.8f));
    g.drawRoundedRectangle(chip.reduced(0.75f), 10.0f, 1.5f);
    drawCategoryIcon(g, chip.reduced(9.0f), entry.category, colour, 1.7f);

    text.removeFromLeft(10);

    // Star on the right; the source line's format tag stays an acronym.
    const auto starArea = text.removeFromRight(36).toFloat().withSizeKeepingCentre(18.0f, 18.0f);
    drawStar(g, starArea, isStarred(entry.description));

    auto lines = text;
    lines.removeFromTop(8);
    g.setColour(theme::colours::text);
    g.setFont(theme::fonts::ui(15.0f, 600));
    g.drawText(entry.description.name, lines.removeFromTop(18), juce::Justification::centredLeft,
               true);

    g.setColour(theme::colours::textFaint);
    g.setFont(theme::fonts::ui(10.5f, 500));
    const auto vendor =
        entry.isBuiltIn ? juce::String("BlockRig") : entry.description.manufacturerName;
    g.drawText(vendor + juce::String::fromUTF8(" \xc2\xb7 ") + formatBadge(entry.description, entry.isBuiltIn),
               lines.removeFromTop(14), juce::Justification::centredLeft, true);
}

void BlockPicker::listBoxItemClicked(int row, const juce::MouseEvent& event)
{
    if (!juce::isPositiveAndBelow(row, static_cast<int>(mEntries.size())))
        return;

    auto& entry = mEntries[static_cast<size_t>(row)];

    if (entry.isHeader)
    {
        mList.deselectAllRows();
        return;
    }

    // The star toggles without selecting or adding.
    if (event.x > mList.getWidth() - kStarZone)
    {
        toggleStar(entry.description);
        mList.repaintRow(row);
    }
}

void BlockPicker::listBoxItemDoubleClicked(int row, const juce::MouseEvent& event)
{
    if (event.x > mList.getWidth() - kStarZone)
        return; // both clicks landed on the star

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
