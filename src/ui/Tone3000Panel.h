#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "blocks/nam/CaptureLibrary.h"
#include "net/Tone3000Client.h"

namespace blockrig
{

/// In-app browser for TONE3000: search their library, download straight into
/// the capture library's TONE3000 folder.
///
/// First run needs two things, both free and both persisted: the app's
/// publishable key (created at tone3000.com/api) and a sign-in, which is what
/// makes downloads valid. The panel walks through both instead of hiding them
/// in a preferences pane, because "why is download greyed out" must answer
/// itself.
class Tone3000Panel final : public juce::Component
{
public:
    Tone3000Panel();
    /// Out of line: Model is incomplete here and unique_ptr needs its size.
    ~Tone3000Panel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kPreferredWidth = 560;
    static constexpr int kPreferredHeight = 460;

private:
    class Model;

    void runSearch();
    void refreshAccountRow();
    void downloadRow(int row);
    void setStatus(const juce::String& text, bool isError);

    juce::SharedResourcePointer<CaptureLibrary> mLibrary;
    std::unique_ptr<Tone3000Client> mClient;

    juce::TextEditor mSearch;
    juce::TextButton mSearchButton{"Search"};
    juce::ListBox mList;
    std::unique_ptr<Model> mModel;
    juce::Array<Tone3000Client::Tone> mTones;

    juce::Label mStatus;
    juce::TextEditor mKeyEditor;
    juce::TextButton mSaveKey{"Save key"};
    juce::TextButton mSignIn{"Sign in"};
    juce::TextButton mOpenSite{"tone3000.com"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Tone3000Panel)
};

} // namespace blockrig
