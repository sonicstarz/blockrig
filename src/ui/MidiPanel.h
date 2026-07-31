#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BlockRigProcessor.h"

namespace blockrig
{

/// The MIDI mappings editor: what each CC steers, and MIDI learn.
///
/// Flow, optimised for a pedalboard on the floor: Add → pick a target from the
/// menu (global actions, or any parameter of any block) → the row arms itself →
/// wiggle the controller → mapped. Rows show live state, so an armed row
/// visibly waits and a learned row shows its CC number.
class MidiPanel final : public juce::Component
{
public:
    explicit MidiPanel(BlockRigProcessor& processor);
    ~MidiPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kPreferredWidth = 520;
    static constexpr int kPreferredHeight = 380;

private:
    class Model;

    void showAddMenu();
    void rowClicked(int row, bool isPopup);
    void refresh();

    BlockRigProcessor& mProcessor;

    juce::TextButton mAddButton{"Add mapping..."};
    juce::Label mHint;
    juce::ListBox mList;
    std::unique_ptr<Model> mModel;
    std::vector<MidiEngine::Mapping> mMappings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPanel)
};

} // namespace blockrig
