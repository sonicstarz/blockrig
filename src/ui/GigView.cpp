#include "ui/GigView.h"

#include "ui/BlockCategories.h"
#include "ui/Theme.h"

namespace blockrig
{
namespace
{
constexpr int kHeaderHeight = 72;
constexpr int kColumnWidth = 320;
constexpr int kPadColumns = 4;
constexpr int kMinPadHeight = 130;

juce::String sceneLetter(int index)
{
    return juce::String::charToString(static_cast<juce::juce_wchar>('A' + (index % 26)));
}
} // namespace

//==============================================================================
GigView::ScenePad::ScenePad(int index, juce::String name, juce::Array<juce::Colour> dots)
    : mIndex(index)
    , mName(std::move(name))
    , mDots(std::move(dots))
{
}

void GigView::ScenePad::setActive(bool isActive)
{
    if (mActive == isActive)
        return;

    mActive = isActive;
    repaint();
}

void GigView::ScenePad::setContent(juce::String name, juce::Array<juce::Colour> dots)
{
    mName = std::move(name);
    mDots = std::move(dots);
    repaint();
}

void GigView::ScenePad::mouseUp(const juce::MouseEvent& event)
{
    if (contains(event.getPosition()) && onClick)
        onClick();
}

void GigView::ScenePad::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(6.0f);
    const auto radius = theme::metrics::radiusXl;

    if (mActive)
    {
        // Active pad: amber wash, thick border, strong glow.
        g.setColour(theme::colours::accent.withAlpha(0.22f));
        g.fillRoundedRectangle(bounds.expanded(4.0f), radius + 4.0f);
        g.setColour(theme::colours::accent.withAlpha(0.14f));
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(theme::colours::accent);
        g.drawRoundedRectangle(bounds.reduced(1.25f), radius, 2.5f);
    }
    else
    {
        g.setColour(theme::colours::panel);
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(theme::colours::outline);
        g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
    }

    auto inner = bounds.reduced(18.0f, 16.0f);

    // Letter top-left; the active pad says so in words as well as colour.
    g.setColour(mActive ? theme::colours::accent : theme::colours::textFaint);
    g.setFont(theme::fonts::mono(13.0f, 600));
    g.drawText(mActive ? sceneLetter(mIndex) + juce::String::fromUTF8("  \xc2\xb7  active")
                       : sceneLetter(mIndex),
               inner.removeFromTop(18.0f), juce::Justification::topLeft, false);

    // Name sits on the baseline, with the dots beneath it.
    auto dots = inner.removeFromBottom(14.0f);
    g.setColour(mActive ? theme::colours::text : theme::colours::textDim);
    g.setFont(theme::fonts::ui(20.0f, 700));
    g.drawText(mName, inner.removeFromBottom(30.0f), juce::Justification::bottomLeft, true);

    auto x = dots.getX();
    for (const auto& colour : mDots)
    {
        g.setColour(colour);
        g.fillRoundedRectangle(x, dots.getY(), 11.0f, 11.0f, 3.5f);
        x += 16.0f;
    }
}

//==============================================================================
void GigView::AddPad::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(6.0f);
    const auto radius = theme::metrics::radiusXl;

    juce::Path outline;
    outline.addRoundedRectangle(bounds, radius);
    const float dashes[] = {6.0f, 5.0f};
    juce::PathStrokeType(1.4f).createDashedStroke(outline, outline, dashes, 2);
    g.setColour(theme::colours::outlineStrong);
    g.fillPath(outline);

    g.setColour(theme::colours::textFaint);
    g.setFont(theme::fonts::ui(26.0f, 500));
    g.drawText("+", bounds, juce::Justification::centred, false);
}

void GigView::AddPad::mouseUp(const juce::MouseEvent& event)
{
    if (contains(event.getPosition()) && onClick)
        onClick();
}

//==============================================================================
GigView::GigView(BlockRigProcessor& processor)
    : mProcessor(processor)
{
    mTuner.onClick = [this] {
        if (onToggleTuner)
            onToggleTuner();
    };
    addAndMakeVisible(mTuner);

    mExit.onClick = [this] {
        if (onExit)
            onExit();
    };
    addAndMakeVisible(mExit);

    mPrevSong.onClick = [this] {
        if (onStepRig)
            onStepRig(-1);
    };
    addAndMakeVisible(mPrevSong);

    mNextSong.onClick = [this] {
        if (onStepRig)
            onStepRig(1);
    };
    addAndMakeVisible(mNextSong);

    // On stage the "+" saves what you are hearing, with a name you can rename
    // later — a dialog here would be the wrong thing to meet mid-song.
    mAddPad.onClick = [this] {
        auto& bank = mProcessor.getSnapshots();

        juce::StringArray uids;
        for (auto* block : mProcessor.getChain().getBlocks())
            uids.add(block->getUid());

        bank.getSnapshots().push_back(snapshots::Bank::capture(
            mProcessor, "Scene " + juce::String(static_cast<int>(bank.getSnapshots().size() + 1)), uids,
            true, false));
        bank.activeIndex = static_cast<int>(bank.getSnapshots().size()) - 1;

        refresh();
        resized();
    };
    addAndMakeVisible(mAddPad);

    refresh();
    startTimerHz(15);
}

GigView::~GigView()
{
    stopTimer();
}

juce::Array<juce::Colour> GigView::dotsForScene(size_t index) const
{
    juce::Array<juce::Colour> colours;

    const auto& snapshots = mProcessor.getSnapshots().getSnapshots();
    if (index >= snapshots.size())
        return colours;

    // The scene's saved block uids, in chain order rather than map order, so
    // the dots read left-to-right like the rig does.
    for (auto* block : mProcessor.getChain().getBlocks())
    {
        if (colours.size() >= 5)
            break;

        if (snapshots[index].blockStates.count(block->getUid()) == 0)
            continue;

        juce::PluginDescription description = block->getMissingDescription();
        if (auto* plugin = block->getPlugin())
            plugin->fillInPluginDescription(description);

        const auto colour = getCategoryColour(categoriseBlock(description));
        if (!colours.contains(colour))
            colours.add(colour);
    }

    return colours;
}

void GigView::refresh()
{
    const auto& snapshots = mProcessor.getSnapshots().getSnapshots();

    if (mScenePads.size() != snapshots.size())
    {
        mScenePads.clear();

        for (size_t i = 0; i < snapshots.size(); ++i)
        {
            auto pad = std::make_unique<ScenePad>(static_cast<int>(i), snapshots[i].name,
                                                  dotsForScene(i));
            const auto index = static_cast<int>(i);

            pad->onClick = [this, index] {
                auto& bank = mProcessor.getSnapshots();

                if (index < static_cast<int>(bank.getSnapshots().size()))
                {
                    snapshots::Bank::apply(mProcessor, bank.getSnapshots()[static_cast<size_t>(index)]);
                    bank.activeIndex = index;
                    refresh();
                }
            };

            addAndMakeVisible(*pad);
            mScenePads.push_back(std::move(pad));
        }

        resized();
    }

    const auto active = mProcessor.getSnapshots().activeIndex;

    for (size_t i = 0; i < mScenePads.size(); ++i)
    {
        mScenePads[i]->setContent(snapshots[i].name, dotsForScene(i));
        mScenePads[i]->setActive(static_cast<int>(i) == active);
    }

    mTuner.setToggleState(mProcessor.isTunerActive(), juce::dontSendNotification);
    repaint();
}

void GigView::timerCallback()
{
    refresh();
}

void GigView::mouseUp(const juce::MouseEvent& event)
{
    // Tapping a section recalls that scene: sections ARE scenes here.
    for (size_t i = 0; i < mSectionRows.size(); ++i)
    {
        if (!mSectionRows[i].contains(event.position))
            continue;

        auto& bank = mProcessor.getSnapshots();
        if (i < bank.getSnapshots().size())
        {
            snapshots::Bank::apply(mProcessor, bank.getSnapshots()[i]);
            bank.activeIndex = static_cast<int>(i);
            refresh();
        }
        return;
    }
}

void GigView::paint(juce::Graphics& g)
{
    g.fillAll(theme::colours::background);

    auto bounds = getLocalBounds().toFloat();
    paintHeader(g, bounds.removeFromTop(static_cast<float>(kHeaderHeight)));
    paintSetlistColumn(g, mColumnArea);
}

void GigView::paintHeader(juce::Graphics& g, juce::Rectangle<float> header)
{
    g.setColour(theme::colours::hairline);
    g.fillRect(header.getX(), header.getBottom() - 1.0f, header.getWidth(), 1.0f);

    auto area = header.reduced(theme::metrics::padding, 0.0f);

    // "Gig mode" badge.
    const auto badge = area.removeFromLeft(104.0f).withSizeKeepingCentre(104.0f, 34.0f);
    g.setColour(theme::colours::accent.withAlpha(0.14f));
    g.fillRoundedRectangle(badge, theme::metrics::radiusSm);
    g.setColour(theme::colours::accent.withAlpha(0.7f));
    g.drawRoundedRectangle(badge.reduced(0.5f), theme::metrics::radiusSm, 1.0f);
    g.setColour(theme::colours::accent);
    g.setFont(theme::fonts::ui(13.0f, 700));
    g.drawText("Gig mode", badge, juce::Justification::centred, false);

    // Where we are in the set, when there is one.
    area.removeFromLeft(14.0f);
    if (setlistName.isNotEmpty())
    {
        const auto dot = juce::String::fromUTF8("  \xc2\xb7  ");
        auto text = "Setlist" + dot + setlistName;
        if (setlistCount > 0)
            text += dot + juce::String(setlistIndex) + "/" + juce::String(setlistCount);

        g.setColour(theme::colours::textFaint);
        g.setFont(theme::fonts::mono(13.0f));
        g.drawText(text, area.removeFromLeft(360.0f), juce::Justification::centredLeft, true);
    }

    // BPM sits left of the buttons, which resized() has already placed: the
    // unit first, then the value in the space left of it.
    auto bpm = area.withTrimmedRight(static_cast<float>(mTuner.getWidth() + mExit.getWidth() + 30));

    const auto unitFont = theme::fonts::ui(11.0f, 500);
    const auto unitWidth = juce::GlyphArrangement::getStringWidth(unitFont, "bpm") + 6.0f;

    g.setColour(theme::colours::textFaint);
    g.setFont(unitFont);
    g.drawText("bpm", bpm.removeFromRight(unitWidth), juce::Justification::centredRight, false);

    g.setColour(theme::colours::text);
    g.setFont(theme::fonts::mono(19.0f, 500));
    g.drawText(juce::String(mProcessor.getTransport().getBpm(), 0), bpm,
               juce::Justification::centredRight, false);
}

void GigView::paintSetlistColumn(juce::Graphics& g, juce::Rectangle<float> column)
{
    mSectionRows.clear();

    auto area = column.reduced(theme::metrics::padding, 0.0f);

    // The song: whichever rig is loaded.
    g.setColour(theme::colours::text);
    g.setFont(theme::fonts::ui(26.0f, 700));
    g.drawText(rigName, area.removeFromTop(48.0f), juce::Justification::centredLeft, true);
    area.removeFromTop(8.0f);

    // Sections: the rig's scenes in order. Everything before the active one is
    // done, the active one is playing, the rest are still to come.
    const auto& snapshots = mProcessor.getSnapshots().getSnapshots();
    const auto active = mProcessor.getSnapshots().activeIndex;

    if (snapshots.empty())
    {
        g.setColour(theme::colours::textGhost);
        g.setFont(theme::fonts::ui(13.0f));
        g.drawText("No scenes in this rig yet", area.removeFromTop(40.0f),
                   juce::Justification::centredLeft, true);
        return;
    }

    // The prev/next buttons own the bottom of the column.
    area.removeFromBottom(56.0f);

    for (size_t i = 0; i < snapshots.size(); ++i)
    {
        if (area.getHeight() < 52.0f)
            break;

        auto row = area.removeFromTop(52.0f).reduced(0.0f, 4.0f);
        mSectionRows.push_back(row);

        const bool isActive = static_cast<int>(i) == active;
        const bool done = active >= 0 && static_cast<int>(i) < active;

        if (isActive)
        {
            g.setColour(theme::colours::accent.withAlpha(0.12f));
            g.fillRoundedRectangle(row, theme::metrics::radiusMd);
            g.setColour(theme::colours::accent);
            g.drawRoundedRectangle(row.reduced(0.75f), theme::metrics::radiusMd, 1.5f);
        }
        else
        {
            g.setColour(theme::colours::panel);
            g.fillRoundedRectangle(row, theme::metrics::radiusMd);
        }

        auto marker = row.removeFromLeft(38.0f);
        if (isActive)
        {
            juce::Path play;
            play.addTriangle(marker.getCentreX() - 4.0f, marker.getCentreY() - 6.0f,
                             marker.getCentreX() - 4.0f, marker.getCentreY() + 6.0f,
                             marker.getCentreX() + 6.0f, marker.getCentreY());
            g.setColour(theme::colours::accent);
            g.fillPath(play);
        }
        else if (done)
        {
            g.setColour(theme::colours::textGhost);
            g.setFont(theme::fonts::ui(14.0f));
            g.drawText(juce::String::fromUTF8("\xe2\x9c\x93"), marker, juce::Justification::centred,
                       false);
        }

        g.setColour(isActive ? theme::colours::text
                    : done   ? theme::colours::textFaint
                             : theme::colours::textDim);
        g.setFont(theme::fonts::ui(17.0f, isActive ? 700 : 400));
        g.drawText(snapshots[i].name, row, juce::Justification::centredLeft, true);
    }
}

void GigView::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop(kHeaderHeight).reduced(theme::metrics::padding, 0);
    mExit.setBounds(header.removeFromRight(92).withSizeKeepingCentre(92, 38));
    header.removeFromRight(10);
    mTuner.setBounds(header.removeFromRight(96).withSizeKeepingCentre(96, 38));

    auto column = area.removeFromLeft(kColumnWidth);
    mColumnArea = column.toFloat();

    auto songButtons = column.removeFromBottom(56).reduced(theme::metrics::padding, 8);
    mPrevSong.setBounds(songButtons.removeFromLeft(songButtons.getWidth() / 2 - 5));
    songButtons.removeFromLeft(10);
    mNextSong.setBounds(songButtons);

    // Scene pads: four across, two rows' worth of height (4j's grid), so a rig
    // with one scene does not get a pad the size of the screen. Extra rows keep
    // that height and simply run on.
    auto grid = area.reduced(theme::metrics::gap, theme::metrics::gap);
    const auto count = static_cast<int>(mScenePads.size()) + 1; // + the add pad
    const auto rows = juce::jmax(2, (count + kPadColumns - 1) / kPadColumns);

    const auto cellWidth = grid.getWidth() / kPadColumns;
    const auto cellHeight = juce::jlimit(kMinPadHeight, 280, grid.getHeight() / rows);

    const auto place = [&](juce::Component& pad, int index) {
        pad.setBounds(grid.getX() + (index % kPadColumns) * cellWidth,
                      grid.getY() + (index / kPadColumns) * cellHeight, cellWidth, cellHeight);
    };

    for (int i = 0; i < static_cast<int>(mScenePads.size()); ++i)
        place(*mScenePads[static_cast<size_t>(i)], i);

    place(mAddPad, static_cast<int>(mScenePads.size()));
}

} // namespace blockrig
