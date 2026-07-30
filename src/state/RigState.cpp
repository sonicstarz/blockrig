#include "state/RigState.h"

#include <tuple>

#include "BlockRigProcessor.h"

namespace blockrig::rigstate
{
namespace
{
constexpr const char* kAppVersion = "0.2.0";

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

juce::ValueTree describeBlock(BlockInstance& block)
{
    juce::ValueTree tree(ids::block);

    auto* plugin = block.getPlugin();
    if (plugin == nullptr)
        return tree;

    juce::PluginDescription description;
    plugin->fillInPluginDescription(description);

    tree.setProperty(ids::blockUid, block.getUid(), nullptr);
    tree.setProperty(ids::format, description.pluginFormatName, nullptr);
    tree.setProperty(ids::identifier, description.fileOrIdentifier, nullptr);
    // Stored as a string: uniqueId is a 32-bit id that does not survive var's
    // signed-int round trip cleanly for every plug-in.
    tree.setProperty(ids::uniqueId, juce::String(description.uniqueId), nullptr);
    tree.setProperty(ids::name, description.name, nullptr);
    tree.setProperty(ids::manufacturer, description.manufacturerName, nullptr);
    tree.setProperty(ids::version, description.version, nullptr);
    tree.setProperty(ids::bypassed, block.isBypassed(), nullptr);

    // Opaque, straight from the plug-in. Never construct this by hand.
    juce::MemoryBlock chunk;
    plugin->getStateInformation(chunk);

    if (!chunk.isEmpty())
    {
        juce::ValueTree stateTree(ids::state);
        stateTree.setProperty(ids::encoding, "base64", nullptr);
        stateTree.setProperty("data", toBase64(chunk), nullptr);
        tree.appendChild(stateTree, nullptr);
    }

    return tree;
}

/// Walks the saved lane and rebuilds it one block at a time, because plug-in
/// creation is asynchronous and lane order has to be deterministic.
class SequentialRestore final : public std::enable_shared_from_this<SequentialRestore>
{
public:
    struct SavedBlock
    {
        juce::PluginDescription description;
        juce::MemoryBlock state;
        bool bypassed = false;
        BlockPosition position;
    };

    SequentialRestore(BlockRigProcessor& processor, std::vector<SavedBlock> blocks,
                      std::vector<std::tuple<int, int, float, float>> rowSettings,
                      std::vector<std::pair<int, juce::String>> stageModes,
                      std::function<void(RestoreResult)> onFinished)
        : mProcessor(processor)
        , mBlocks(std::move(blocks))
        , mRowSettings(std::move(rowSettings))
        , mStageModes(std::move(stageModes))
        , mOnFinished(std::move(onFinished))
    {
        mResult.blocksRequested = static_cast<int>(mBlocks.size());
    }

    void start() { step(); }

private:
    void step()
    {
        if (mIndex >= static_cast<int>(mBlocks.size()))
        {
            // Row gain and pan last: the rows have to exist first.
            auto& chain = mProcessor.getChain();

            for (const auto& [stageIndex, rowIndex, gainDb, pan] : mRowSettings)
            {
                if (rowIndex > 0 && !chain.isStageSplit(stageIndex))
                    continue;
                chain.setRowGainDb(stageIndex, rowIndex, gainDb);
                chain.setRowPan(stageIndex, rowIndex, pan);
            }

            for (const auto& [stageIndex, mode] : mStageModes)
                chain.setStageMode(stageIndex, mode == "parallel" ? BlockChain::StageMode::parallel
                                                                  : BlockChain::StageMode::dualMono);

            if (mOnFinished)
                mOnFinished(mResult);
            return;
        }

        const auto& saved = mBlocks[static_cast<size_t>(mIndex)];
        auto self = shared_from_this();

        // Make sure the stage exists (and is split) before the block lands in it.
        auto& chain = mProcessor.getChain();
        while (chain.getNumStages() <= saved.position.stage)
            chain.appendEmptyStage();

        if (saved.position.row > 0 && !chain.isStageSplit(saved.position.stage))
            chain.splitStage(saved.position.stage);

        mProcessor.addBlock(saved.description, saved.position, [self](juce::String uid, juce::String error) {
            self->blockFinished(std::move(uid), std::move(error));
        });
    }

    void blockFinished(juce::String uid, juce::String error)
    {
        const auto& saved = mBlocks[static_cast<size_t>(mIndex)];

        if (uid.isEmpty())
        {
            // A missing plug-in must not sink the rig: note it and carry on, so
            // the user can reinstall and reload.
            mResult.missingPlugins.add(saved.description.name
                                       + (error.isNotEmpty() ? " (" + error + ")" : juce::String()));
        }
        else
        {
            ++mResult.blocksCreated;

            if (auto* block = mProcessor.getChain().getBlockByUid(uid))
            {
                block->setBypassed(saved.bypassed);

                if (auto* plugin = block->getPlugin(); plugin != nullptr && !saved.state.isEmpty())
                {
                    plugin->setStateInformation(saved.state.getData(), static_cast<int>(saved.state.getSize()));

                    // A refused chunk produces no error, so check rather than
                    // trust: if the plug-in hands back something completely
                    // different, its settings did not come back.
                    juce::MemoryBlock readBack;
                    plugin->getStateInformation(readBack);

                    if (readBack.isEmpty() && !saved.state.isEmpty())
                        mResult.stateNotRestored.add(saved.description.name);
                }
            }
        }

        ++mIndex;
        step();
    }

    BlockRigProcessor& mProcessor;
    std::vector<SavedBlock> mBlocks;
    std::vector<std::tuple<int, int, float, float>> mRowSettings;
    std::vector<std::pair<int, juce::String>> mStageModes;
    std::function<void(RestoreResult)> mOnFinished;
    RestoreResult mResult;
    int mIndex = 0;
};
} // namespace

juce::ValueTree toValueTree(BlockRigProcessor& processor)
{
    juce::ValueTree rig(ids::root);
    rig.setProperty(ids::schemaVersion, kSchemaVersion, nullptr);
    rig.setProperty(ids::appVersion, kAppVersion, nullptr);
    rig.setProperty(ids::uuid, juce::Uuid().toDashedString(), nullptr);
    rig.setProperty(ids::modifiedUtc, juce::Time::getCurrentTime().toISO8601(true), nullptr);

    juce::ValueTree input(ids::input);
    input.setProperty(ids::inputModeAttr,
                      processor.getInputMode() == BlockRigProcessor::InputMode::mono ? "mono" : "stereo", nullptr);
    input.setProperty(ids::gainDb, processor.getInputGainDb(), nullptr);
    rig.appendChild(input, nullptr);

    juce::ValueTree output(ids::output);
    output.setProperty(ids::gainDb, processor.getOutputGainDb(), nullptr);
    rig.appendChild(output, nullptr);

    juce::ValueTree transport("Transport");
    transport.setProperty("bpm", processor.getTransport().getBpm(), nullptr);
    transport.setProperty("timeSigNumerator", processor.getTransport().getTimeSignatureNumerator(), nullptr);
    transport.setProperty("timeSigDenominator", processor.getTransport().getTimeSignatureDenominator(),
                          nullptr);
    rig.appendChild(transport, nullptr);

    // Stage per chain stage, Row per parallel path. A split writes two rows.
    juce::ValueTree lane(ids::lane);
    auto& chain = processor.getChain();

    for (int stageIndex = 0; stageIndex < chain.getNumStages(); ++stageIndex)
    {
        juce::ValueTree stage(ids::stage);
        stage.setProperty("mode", chain.getStageMode(stageIndex) == BlockChain::StageMode::dualMono
                                      ? "dualMono"
                                      : "parallel",
                          nullptr);

        for (int rowIndex = 0; rowIndex < chain.getNumRows(stageIndex); ++rowIndex)
        {
            juce::ValueTree row(ids::row);
            row.setProperty(ids::gainDb, chain.getRowGainDb(stageIndex, rowIndex), nullptr);
            row.setProperty("pan", chain.getRowPan(stageIndex, rowIndex), nullptr);

            for (auto* block : chain.getBlocksInRow(stageIndex, rowIndex))
                row.appendChild(describeBlock(*block), nullptr);

            stage.appendChild(row, nullptr);
        }

        lane.appendChild(stage, nullptr);
    }

    rig.appendChild(lane, nullptr);
    return rig;
}

void restore(BlockRigProcessor& processor, const juce::ValueTree& rig,
             std::function<void(RestoreResult)> onFinished)
{
    RestoreResult result;

    if (!rig.isValid() || !rig.hasType(ids::root))
    {
        result.error = "Not a BlockRig rig.";
        if (onFinished)
            onFinished(result);
        return;
    }

    const int version = static_cast<int>(rig.getProperty(ids::schemaVersion, 0));
    if (version > kSchemaVersion)
    {
        result.error = "This rig was made in a newer version of BlockRig.";
        if (onFinished)
            onFinished(result);
        return;
    }

    const auto input = rig.getChildWithName(ids::input);
    if (input.isValid())
    {
        processor.setInputMode(input.getProperty(ids::inputModeAttr).toString() == "stereo"
                                   ? BlockRigProcessor::InputMode::stereo
                                   : BlockRigProcessor::InputMode::mono);
        processor.setInputGainDb(static_cast<float>(input.getProperty(ids::gainDb, 0.0)));
    }

    const auto output = rig.getChildWithName(ids::output);
    if (output.isValid())
        processor.setOutputGainDb(static_cast<float>(output.getProperty(ids::gainDb, 0.0)));

    if (const auto transport = rig.getChildWithName("Transport"); transport.isValid())
    {
        processor.getTransport().setBpm(static_cast<double>(transport.getProperty("bpm", 120.0)));
        processor.getTransport().setTimeSignature(
            static_cast<int>(transport.getProperty("timeSigNumerator", 4)),
            static_cast<int>(transport.getProperty("timeSigDenominator", 4)));
    }

    // Collect the saved lane before touching the live one.
    std::vector<SequentialRestore::SavedBlock> saved;
    std::vector<std::tuple<int, int, float, float>> rowSettings; // stage, row, gain, pan
    std::vector<std::pair<int, juce::String>> stageModes;
    const auto lane = rig.getChildWithName(ids::lane);

    for (int stageIndex = 0; stageIndex < lane.getNumChildren(); ++stageIndex)
    {
        const auto stage = lane.getChild(stageIndex);
        stageModes.emplace_back(stageIndex, stage.getProperty("mode", "dualMono").toString());

        for (int rowIndex = 0; rowIndex < stage.getNumChildren(); ++rowIndex)
        {
            const auto row = stage.getChild(rowIndex);

            rowSettings.emplace_back(stageIndex, rowIndex,
                                     static_cast<float>(row.getProperty(ids::gainDb, 0.0)),
                                     static_cast<float>(row.getProperty("pan", 0.0)));

            int indexInRow = 0;

            for (int blockIndex = 0; blockIndex < row.getNumChildren(); ++blockIndex)
            {
                const auto blockTree = row.getChild(blockIndex);
                if (!blockTree.hasType(ids::block))
                    continue;

                SequentialRestore::SavedBlock entry;
                entry.position = BlockPosition{stageIndex, rowIndex, indexInRow++};
                entry.description.pluginFormatName = blockTree.getProperty(ids::format).toString();
                entry.description.fileOrIdentifier = blockTree.getProperty(ids::identifier).toString();
                entry.description.name = blockTree.getProperty(ids::name).toString();
                entry.description.manufacturerName = blockTree.getProperty(ids::manufacturer).toString();
                entry.description.version = blockTree.getProperty(ids::version).toString();
                entry.description.uniqueId = entry.description.deprecatedUid =
                    blockTree.getProperty(ids::uniqueId).toString().getIntValue();
                entry.bypassed = static_cast<bool>(blockTree.getProperty(ids::bypassed, false));

                const auto stateTree = blockTree.getChildWithName(ids::state);
                if (stateTree.isValid())
                    entry.state = fromBase64(stateTree.getProperty("data").toString());

                saved.push_back(std::move(entry));
            }
        }
    }

    // Every plug-in in the old chain is about to be destroyed.
    processor.notifyBlockRemoval({});
    processor.getChain().clear();

    auto sequence = std::make_shared<SequentialRestore>(processor, std::move(saved), std::move(rowSettings),
                                                       std::move(stageModes), std::move(onFinished));
    sequence->start();
}

} // namespace blockrig::rigstate
