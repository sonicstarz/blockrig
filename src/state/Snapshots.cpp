#include "state/Snapshots.h"

#include "BlockRigProcessor.h"

namespace blockrig::snapshots
{
namespace
{
juce::String toBase64(const juce::MemoryBlock& block)
{
    return juce::Base64::toBase64(block.getData(), block.getSize());
}

juce::MemoryBlock fromBase64(const juce::String& text)
{
    juce::MemoryOutputStream stream;
    juce::Base64::convertFromBase64(stream, text);
    return stream.getMemoryBlock();
}
} // namespace

Snapshot Bank::capture(BlockRigProcessor& processor, juce::String name, const juce::StringArray& uids,
                       bool includeTempo, bool includeTuner)
{
    Snapshot snapshot;
    snapshot.name = std::move(name);
    snapshot.includeTempo = includeTempo;
    snapshot.includeTuner = includeTuner;

    for (auto* block : processor.getChain().getBlocks())
    {
        if (!uids.contains(block->getUid()))
            continue;

        if (auto* plugin = block->getPlugin())
        {
            juce::MemoryBlock chunk;
            plugin->getStateInformation(chunk);
            snapshot.blockStates[block->getUid()] = std::move(chunk);
        }
    }

    snapshot.bpm = processor.getTransport().getBpm();
    snapshot.timeSigNumerator = processor.getTransport().getTimeSignatureNumerator();
    snapshot.timeSigDenominator = processor.getTransport().getTimeSignatureDenominator();
    snapshot.tunerActive = processor.isTunerActive();

    return snapshot;
}

juce::StringArray Bank::apply(BlockRigProcessor& processor, const Snapshot& snapshot)
{
    juce::StringArray applied;

    for (auto* block : processor.getChain().getBlocks())
    {
        const auto found = snapshot.blockStates.find(block->getUid());
        if (found == snapshot.blockStates.end())
            continue;

        if (auto* plugin = block->getPlugin(); plugin != nullptr && !found->second.isEmpty())
        {
            plugin->setStateInformation(found->second.getData(),
                                        static_cast<int>(found->second.getSize()));
            applied.add(block->getDisplayName());
        }
    }

    // A restored chunk can rearrange a plug-in's buses; the lane walk puts any
    // drifted layout back before it is audible.
    processor.getChain().prepareLane(false);

    if (snapshot.includeTempo)
    {
        processor.getTransport().setBpm(snapshot.bpm);
        processor.getTransport().setTimeSignature(snapshot.timeSigNumerator, snapshot.timeSigDenominator);
    }

    return applied;
}

juce::ValueTree Bank::toValueTree() const
{
    juce::ValueTree bank("Snapshots");

    for (const auto& snapshot : mSnapshots)
    {
        juce::ValueTree tree("Snapshot");
        tree.setProperty("name", snapshot.name, nullptr);
        tree.setProperty("includeTempo", snapshot.includeTempo, nullptr);
        tree.setProperty("includeTuner", snapshot.includeTuner, nullptr);
        tree.setProperty("bpm", snapshot.bpm, nullptr);
        tree.setProperty("timeSigNumerator", snapshot.timeSigNumerator, nullptr);
        tree.setProperty("timeSigDenominator", snapshot.timeSigDenominator, nullptr);
        tree.setProperty("tunerActive", snapshot.tunerActive, nullptr);

        for (const auto& [uid, state] : snapshot.blockStates)
        {
            juce::ValueTree blockTree("BlockState");
            blockTree.setProperty("uid", uid, nullptr);
            blockTree.setProperty("data", toBase64(state), nullptr);
            tree.appendChild(blockTree, nullptr);
        }

        bank.appendChild(tree, nullptr);
    }

    return bank;
}

void Bank::restoreFrom(const juce::ValueTree& bank)
{
    mSnapshots.clear();
    activeIndex = -1;

    for (const auto& tree : bank)
    {
        if (!tree.hasType("Snapshot"))
            continue;

        Snapshot snapshot;
        snapshot.name = tree.getProperty("name", "Snapshot").toString();
        snapshot.includeTempo = tree.getProperty("includeTempo", true);
        snapshot.includeTuner = tree.getProperty("includeTuner", true);
        snapshot.bpm = tree.getProperty("bpm", 120.0);
        snapshot.timeSigNumerator = tree.getProperty("timeSigNumerator", 4);
        snapshot.timeSigDenominator = tree.getProperty("timeSigDenominator", 4);
        snapshot.tunerActive = tree.getProperty("tunerActive", false);

        for (const auto& blockTree : tree)
            if (blockTree.hasType("BlockState"))
                snapshot.blockStates[blockTree.getProperty("uid").toString()] =
                    fromBase64(blockTree.getProperty("data").toString());

        mSnapshots.push_back(std::move(snapshot));
    }
}

} // namespace blockrig::snapshots
