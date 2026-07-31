#include "state/UndoHistory.h"

#include "BlockRigProcessor.h"
#include "state/RigState.h"

namespace blockrig
{
namespace
{
juce::MemoryBlock fromBase64(const juce::String& text)
{
    juce::MemoryOutputStream stream;
    juce::Base64::convertFromBase64(stream, text);
    return stream.getMemoryBlock();
}

/// Every block in lane order, as (uid, identifier) — the rig's structure with
/// the settings stripped out.
juce::StringArray describeStructure(const juce::ValueTree& rig)
{
    juce::StringArray structure;

    const auto lane = rig.getChildWithName(rigstate::ids::lane);

    for (const auto& stage : lane)
        for (const auto& row : stage)
            for (const auto& block : row)
                if (block.hasType(rigstate::ids::block))
                    structure.add(block.getProperty(rigstate::ids::blockUid).toString() + "|"
                                  + block.getProperty(rigstate::ids::identifier).toString());

    return structure;
}
} // namespace

UndoHistory::UndoHistory(BlockRigProcessor& processor)
    : mProcessor(processor)
{
}

void UndoHistory::capture(const juce::String& description)
{
    if (mApplying)
        return;

    auto tree = rigstate::toValueTree(mProcessor);

    // Nothing changed since the last point: do not fill the ring with duplicates.
    if (mPosition >= 0 && mEntries[static_cast<size_t>(mPosition)].tree.isEquivalentTo(tree))
        return;

    // A new edit after undoing discards the redo branch, as everywhere else.
    if (mPosition + 1 < static_cast<int>(mEntries.size()))
        mEntries.erase(mEntries.begin() + mPosition + 1, mEntries.end());

    mEntries.push_back({std::move(tree), description});

    if (static_cast<int>(mEntries.size()) > kMaxEntries)
        mEntries.erase(mEntries.begin());

    mPosition = static_cast<int>(mEntries.size()) - 1;
}

juce::String UndoHistory::getUndoDescription() const
{
    // The description of an undo point describes what it will take you back to,
    // which is the state BEFORE the edit named in the entry above it.
    return canUndo() ? mEntries[static_cast<size_t>(mPosition)].description : juce::String();
}

juce::String UndoHistory::getRedoDescription() const
{
    return canRedo() ? mEntries[static_cast<size_t>(mPosition + 1)].description : juce::String();
}

void UndoHistory::undo()
{
    if (!canUndo())
        return;

    --mPosition;
    applyEntry(mEntries[static_cast<size_t>(mPosition)]);
}

void UndoHistory::redo()
{
    if (!canRedo())
        return;

    ++mPosition;
    applyEntry(mEntries[static_cast<size_t>(mPosition)]);
}

void UndoHistory::clear()
{
    mEntries.clear();
    mPosition = -1;
}

bool UndoHistory::structureMatches(const juce::ValueTree& tree) const
{
    return describeStructure(rigstate::toValueTree(mProcessor)) == describeStructure(tree);
}

void UndoHistory::applySettingsOnly(const juce::ValueTree& tree)
{
    // Push each block's saved chunk back into the plug-in that is already
    // loaded. No instantiation, so this is instant even with a heavy rig.
    const auto lane = tree.getChildWithName(rigstate::ids::lane);

    for (const auto& stage : lane)
        for (const auto& row : stage)
            for (const auto& blockTree : row)
            {
                if (!blockTree.hasType(rigstate::ids::block))
                    continue;

                auto* block = mProcessor.getChain().getBlockByUid(
                    blockTree.getProperty(rigstate::ids::blockUid).toString());

                if (block == nullptr)
                    continue;

                block->setBypassed(blockTree.getProperty(rigstate::ids::bypassed, false));

                auto* plugin = block->getPlugin();
                if (plugin == nullptr)
                    continue;

                const auto stateTree = blockTree.getChildWithName(rigstate::ids::state);
                if (!stateTree.isValid())
                    continue;

                const auto chunk = fromBase64(stateTree.getProperty("data").toString());

                if (!chunk.isEmpty())
                    plugin->setStateInformation(chunk.getData(), static_cast<int>(chunk.getSize()));
            }

    // A restored chunk can rearrange a plug-in's buses.
    mProcessor.getChain().prepareLane(false);

    // Row gains, stage modes and transport still come from the tree.
    if (const auto transport = tree.getChildWithName("Transport"); transport.isValid())
    {
        mProcessor.getTransport().setBpm(transport.getProperty("bpm", 120.0));
        mProcessor.getTransport().setTimeSignature(transport.getProperty("timeSigNumerator", 4),
                                                   transport.getProperty("timeSigDenominator", 4));
    }

    mProcessor.getSnapshots().restoreFrom(tree.getChildWithName("Snapshots"));
    mProcessor.getMidiEngine().restoreFrom(tree.getChildWithName("MidiMappings"));

    if (mProcessor.onChainChanged)
        mProcessor.onChainChanged();
}

void UndoHistory::applyEntry(const Entry& entry)
{
    mApplying = true;

    if (structureMatches(entry.tree))
    {
        applySettingsOnly(entry.tree);
        mApplying = false;

        if (onApplied)
            onApplied();

        return;
    }

    // Structure changed, so blocks have to be rebuilt. Same path as loading a
    // rig file, including the removal notice that closes editor windows before
    // their plug-ins die.
    rigstate::restore(mProcessor, entry.tree, [this](rigstate::RestoreResult) {
        mApplying = false;

        if (onApplied)
            onApplied();
    });
}

} // namespace blockrig
