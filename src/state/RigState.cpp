#include "state/RigState.h"

#include "state/GraphState.h"
#include "state/RigMigration.h"

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

    // A placeholder for a missing plug-in saves as what it stands for, state
    // and all, so reinstalling the plug-in restores the block intact rather
    // than the rig having quietly forgotten it.
    juce::PluginDescription description;
    juce::MemoryBlock chunk;

    if (plugin != nullptr)
    {
        plugin->fillInPluginDescription(description);
        plugin->getStateInformation(chunk); // opaque; never construct by hand
    }
    else if (block.isMissing() && block.getMissingDescription().name.isNotEmpty())
    {
        description = block.getMissingDescription();
        chunk = block.getMissingState();
    }
    else
    {
        return tree;
    }

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

    if (!chunk.isEmpty())
    {
        juce::ValueTree stateTree(ids::state);
        stateTree.setProperty(ids::encoding, "base64", nullptr);
        stateTree.setProperty("data", toBase64(chunk), nullptr);
        tree.appendChild(stateTree, nullptr);
    }

    return tree;
}

/// Rebuilds the graph one block at a time, because plug-in creation is
/// asynchronous and the order blocks appear in has to be deterministic.
///
/// The structure — nodes, positions, wires — is already in place before this
/// runs: graphstate::applyStructure rebuilds it synchronously from the
/// document, so every node exists with its saved uid and the wires already
/// connect them. All that is left is to fill each node with its plug-in.
class SequentialRestore final : public std::enable_shared_from_this<SequentialRestore>
{
public:
    struct SavedBlock
    {
        juce::String uid;
        juce::PluginDescription description;
        juce::MemoryBlock state;
        bool bypassed = false;
    };

    SequentialRestore(BlockRigProcessor& processor, std::vector<SavedBlock> blocks,
                      std::function<void(RestoreResult)> onFinished)
        : mProcessor(processor)
        , mBlocks(std::move(blocks))
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
            // Widths are negotiated once, at the end: doing it per block would
            // re-prepare the whole rig N times on load.
            mProcessor.getChain().prepareGraph(true);
            mProcessor.getChain().publish();

            if (mOnFinished)
                mOnFinished(mResult);
            return;
        }

        const auto& saved = mBlocks[static_cast<size_t>(mIndex)];
        auto self = shared_from_this();

        mProcessor.createBlockForNode(saved.description, saved.uid,
                                      [self](juce::String error)
                                      { self->blockFinished(std::move(error)); });
    }

    void blockFinished(juce::String error)
    {
        const auto& saved = mBlocks[static_cast<size_t>(mIndex)];
        auto& graph = mProcessor.getChain().getGraph();

        if (graph.getBlockByUid(saved.uid) == nullptr)
        {
            // A missing plug-in must not sink the rig, and must not quietly
            // vanish either: a placeholder holds the node, remembers what
            // belongs there and the state it was saved with, and passes audio
            // through until the plug-in comes back. It keeps the saved uid, so
            // the wires around it still land.
            mResult.missingPlugins.add(saved.description.name
                                       + (error.isNotEmpty() ? " (" + error + ")" : juce::String()));

            auto placeholder = std::make_unique<BlockInstance>(nullptr, saved.uid);
            placeholder->setMissingDescription(saved.description, saved.state);
            placeholder->setBypassed(saved.bypassed);
            graph.setBlockFor(saved.uid, std::move(placeholder));
        }
        else
        {
            ++mResult.blocksCreated;

            if (auto* block = graph.getBlockByUid(saved.uid))
            {
                block->setBypassed(saved.bypassed);

                if (auto* plugin = block->getPlugin(); plugin != nullptr && !saved.state.isEmpty())
                {
                    plugin->setStateInformation(saved.state.getData(),
                                                static_cast<int>(saved.state.getSize()));

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

    if (processor.getAudioStateXml)
    {
        if (const auto audio = processor.getAudioStateXml(); audio.isNotEmpty())
        {
            juce::ValueTree audioTree("AudioSetup");
            audioTree.setProperty("deviceStateXml", audio, nullptr);
            rig.appendChild(audioTree, nullptr);
        }
    }

    rig.appendChild(processor.getSnapshots().toValueTree(), nullptr);
    rig.appendChild(processor.getMidiEngine().toValueTree(), nullptr);

    juce::ValueTree transport("Transport");
    transport.setProperty("bpm", processor.getTransport().getBpm(), nullptr);
    transport.setProperty("timeSigNumerator", processor.getTransport().getTimeSignatureNumerator(), nullptr);
    transport.setProperty("timeSigDenominator", processor.getTransport().getTimeSignatureDenominator(),
                          nullptr);
    rig.appendChild(transport, nullptr);

    // The graph: nodes with grid positions, wires between them. One serialiser
    // for the standalone rig file and the DAW chunk, as before.
    rig.appendChild(graphstate::toValueTree(processor.getChain().getGraph(),
                                            [](BlockInstance& block) { return describeBlock(block); }),
                    nullptr);
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

    if (const auto audio = rig.getChildWithName("AudioSetup");
        audio.isValid() && processor.applyAudioStateXml)
        processor.applyAudioStateXml(audio.getProperty("deviceStateXml").toString());

    processor.getSnapshots().restoreFrom(rig.getChildWithName("Snapshots"));
    processor.getMidiEngine().restoreFrom(rig.getChildWithName("MidiMappings"));

    if (const auto transport = rig.getChildWithName("Transport"); transport.isValid())
    {
        processor.getTransport().setBpm(static_cast<double>(transport.getProperty("bpm", 120.0)));
        processor.getTransport().setTimeSignature(
            static_cast<int>(transport.getProperty("timeSigNumerator", 4)),
            static_cast<int>(transport.getProperty("timeSigDenominator", 4)));
    }

    // Bring the document up to the current schema first, so everything below
    // only ever sees a graph. A v1 rig migrates here; a v2 one passes through.
    const auto current = migrateToCurrent(rig);

    if (!current.isValid())
    {
        result.error = "This rig was made in a newer version of BlockRig.";
        if (onFinished)
            onFinished(result);
        return;
    }

    // Every plug-in in the old graph is about to be destroyed.
    processor.notifyBlockRemoval({});

    // Structure first, synchronously: nodes at their saved positions with their
    // saved uids, and every wire between them. Plug-ins arrive afterwards.
    auto& graph = processor.getChain().getGraph();
    const auto load = graphstate::applyStructure(graph, current.getChildWithName(ids::graph));

    if (load.error.isNotEmpty())
    {
        result.error = load.error;
        if (onFinished)
            onFinished(result);
        return;
    }

    if (load.rejectedWires > 0)
        result.stateNotRestored.add(juce::String(load.rejectedWires)
                                    + " connection(s) in this rig were invalid and were dropped");

    std::vector<SequentialRestore::SavedBlock> saved;

    for (const auto& pending : load.pending)
    {
        const auto& node = pending.source;

        SequentialRestore::SavedBlock entry;
        entry.uid = pending.uid;
        entry.description.pluginFormatName = node.getProperty(ids::format).toString();
        entry.description.fileOrIdentifier = node.getProperty(ids::identifier).toString();
        entry.description.name = node.getProperty(ids::name).toString();
        entry.description.manufacturerName = node.getProperty(ids::manufacturer).toString();
        entry.description.version = node.getProperty(ids::version).toString();
        entry.description.uniqueId = entry.description.deprecatedUid =
            node.getProperty(ids::uniqueId).toString().getIntValue();
        entry.bypassed = static_cast<bool>(node.getProperty(ids::bypassed, false));

        const auto stateTree = node.getChildWithName(ids::state);
        if (stateTree.isValid())
            entry.state = fromBase64(stateTree.getProperty("data").toString());

        saved.push_back(std::move(entry));
    }

    auto sequence =
        std::make_shared<SequentialRestore>(processor, std::move(saved), std::move(onFinished));
    sequence->start();
}

} // namespace blockrig::rigstate
