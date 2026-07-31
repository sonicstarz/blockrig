#include "ui/Tone3000Panel.h"

#include "ui/Theme.h"

namespace blockrig
{
namespace
{
juce::File clientSettingsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support")
        .getChildFile("BlockRig")
        .getChildFile("Tone3000.settings");
}
} // namespace

class Tone3000Panel::Model final : public juce::ListBoxModel
{
public:
    explicit Model(Tone3000Panel& owner)
        : mOwner(owner)
    {
    }

    int getNumRows() override { return mOwner.mTones.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool hovered) override
    {
        if (row >= mOwner.mTones.size())
            return;

        const auto& tone = mOwner.mTones.getReference(row);
        auto area = juce::Rectangle<int>(0, 0, width, height).reduced(2, 2).toFloat();

        g.setColour(hovered ? theme::colours::panelRaised : theme::colours::panel);
        g.fillRoundedRectangle(area, theme::metrics::smallCornerRadius);

        g.setColour(theme::colours::text);
        g.setFont(juce::FontOptions(13.5f, juce::Font::bold));
        g.drawText(tone.title, area.reduced(10.0f, 2.0f).removeFromTop(20.0f),
                   juce::Justification::centredLeft, true);

        g.setColour(theme::colours::textFaint);
        g.setFont(juce::FontOptions(11.0f));
        g.drawText((tone.author.isNotEmpty() ? "by " + tone.author + "   " : juce::String()) + tone.gear,
                   area.reduced(10.0f, 3.0f), juce::Justification::bottomLeft, true);

        g.setColour(theme::colours::accent);
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText("GET", area.removeFromRight(52.0f), juce::Justification::centred, false);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent&) override { mOwner.downloadRow(row); }

private:
    Tone3000Panel& mOwner;
};

//==============================================================================
Tone3000Panel::Tone3000Panel()
    : mClient(std::make_unique<Tone3000Client>(clientSettingsFile()))
{
    mSearch.setTextToShowWhenEmpty("Search TONE3000 - amp, pedal, artist...",
                                   theme::colours::textFaint);
    mSearch.onReturnKey = [this] { runSearch(); };
    addAndMakeVisible(mSearch);

    mSearchButton.onClick = [this] { runSearch(); };
    addAndMakeVisible(mSearchButton);

    mModel = std::make_unique<Model>(*this);
    mList.setModel(mModel.get());
    mList.setRowHeight(44);
    mList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(mList);

    mStatus.setFont(juce::FontOptions(11.5f));
    mStatus.setColour(juce::Label::textColourId, theme::colours::textFaint);
    addAndMakeVisible(mStatus);

    mKeyEditor.setTextToShowWhenEmpty("Paste your free API key (t3k_pub_...)",
                                      theme::colours::textFaint);
    mKeyEditor.setText(mClient->getPublishableKey(), juce::dontSendNotification);
    addAndMakeVisible(mKeyEditor);

    mSaveKey.onClick = [this] {
        mClient->setPublishableKey(mKeyEditor.getText());
        refreshAccountRow();
    };
    addAndMakeVisible(mSaveKey);

    mSignIn.onClick = [this] {
        if (mClient->isSignedIn())
        {
            mClient->signOut();
            refreshAccountRow();
            return;
        }

        setStatus("Waiting for the browser sign-in…", false);
        mClient->signIn([this](bool success, juce::String error) {
            setStatus(success ? "Signed in." : error, !success);
            refreshAccountRow();
        });
    };
    addAndMakeVisible(mSignIn);

    mOpenSite.setTooltip("Open TONE3000 in your browser. Anything you download there lands in the "
                         "capture library automatically.");
    mOpenSite.onClick = [] { juce::URL("https://www.tone3000.com").launchInDefaultBrowser(); };
    addAndMakeVisible(mOpenSite);

    refreshAccountRow();
}

Tone3000Panel::~Tone3000Panel() = default;

void Tone3000Panel::refreshAccountRow()
{
    const bool hasKey = mClient->getPublishableKey().isNotEmpty();

    mSignIn.setEnabled(hasKey);
    mSignIn.setButtonText(mClient->isSignedIn() ? "Sign out" : "Sign in");

    if (!hasKey)
        setStatus("One-time setup: create a free API key at tone3000.com/api, paste it above, then "
                  "sign in. Or just download in your browser - BlockRig imports from Downloads.",
                  false);
    else if (!mClient->isSignedIn())
        setStatus("Key saved. Sign in to search and download.", false);
    else
        setStatus("Signed in"
                      + (mClient->getSignedInUser().isNotEmpty() ? " as " + mClient->getSignedInUser()
                                                                 : juce::String())
                      + ".",
                  false);
}

void Tone3000Panel::setStatus(const juce::String& text, bool isError)
{
    mStatus.setText(text, juce::dontSendNotification);
    mStatus.setColour(juce::Label::textColourId,
                      isError ? theme::colours::bad : theme::colours::textFaint);
}

void Tone3000Panel::runSearch()
{
    const auto query = mSearch.getText().trim();
    if (query.isEmpty())
        return;

    setStatus("Searching…", false);

    mClient->search(query, [this](juce::Array<Tone3000Client::Tone> tones, juce::String error) {
        mTones = std::move(tones);
        mList.updateContent();
        mList.repaint();

        if (error.isNotEmpty())
            setStatus(error, true);
        else
            setStatus(juce::String(mTones.size()) + " result(s). Click one to download it into the "
                                                    "capture library.",
                      false);
    });
}

void Tone3000Panel::downloadRow(int row)
{
    if (row < 0 || row >= mTones.size())
        return;

    const auto tone = mTones[row];
    setStatus("Downloading \"" + tone.title + "\"…", false);

    mClient->downloadModel(tone, [this, tone](juce::File file, juce::String error) {
        if (error.isNotEmpty())
        {
            setStatus(error, true);
            return;
        }

        mLibrary->addCapture(file, "TONE3000");
        file.deleteFile();
        setStatus("\"" + tone.title + "\" is in the capture library (TONE3000 folder).", false);
    });
}

void Tone3000Panel::paint(juce::Graphics& g)
{
    g.fillAll(theme::colours::background);
}

void Tone3000Panel::resized()
{
    auto area = getLocalBounds().reduced(theme::metrics::gap);

    auto searchRow = area.removeFromTop(30);
    mSearchButton.setBounds(searchRow.removeFromRight(76));
    searchRow.removeFromRight(6);
    mSearch.setBounds(searchRow);

    auto accountRow = area.removeFromBottom(30);
    mOpenSite.setBounds(accountRow.removeFromRight(108).reduced(0, 1));
    accountRow.removeFromRight(6);
    mSignIn.setBounds(accountRow.removeFromRight(72).reduced(0, 1));
    accountRow.removeFromRight(6);
    mSaveKey.setBounds(accountRow.removeFromRight(72).reduced(0, 1));
    accountRow.removeFromRight(6);
    mKeyEditor.setBounds(accountRow);

    area.removeFromBottom(4);
    mStatus.setBounds(area.removeFromBottom(34));

    area.removeFromTop(6);
    mList.setBounds(area);
}

} // namespace blockrig
