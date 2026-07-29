#include "BlockRigProcessor.h"
#include "ui/MainView.h"

namespace blockrig
{
namespace
{
/// Wraps the shared interface for the plug-in build. The standalone app builds a
/// MainView directly, because it also has an audio device to hand it.
class HostedEditor final : public juce::AudioProcessorEditor
{
public:
    explicit HostedEditor(BlockRigProcessor& processor)
        : juce::AudioProcessorEditor(&processor)
        , mView(processor, nullptr) // no device manager: the DAW owns the device
    {
        addAndMakeVisible(mView);
        setResizable(true, true);
        setResizeLimits(900, 520, 4000, 2400);
        setSize(mView.getWidth(), mView.getHeight());
    }

    void resized() override { mView.setBounds(getLocalBounds()); }

private:
    MainView mView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostedEditor)
};
} // namespace

/// Defined here rather than alongside the processor so that headless targets can
/// link the processor without dragging in the whole interface.
juce::AudioProcessorEditor* BlockRigProcessor::createEditor()
{
    return new HostedEditor(*this);
}

} // namespace blockrig
