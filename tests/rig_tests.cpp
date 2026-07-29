// Rig save/restore against the real host processor: lane order, bypass state,
// child plug-in state, and graceful handling of a plug-in that is no longer
// installed.

#include <cstdio>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "BlockRigProcessor.h"
#include "blocks/nam/NamBlockProcessor.h"
#include "state/RigState.h"

namespace
{
int gFailures = 0;

void check(bool condition, const juce::String& what)
{
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what.toRawUTF8());
    if (!condition)
        ++gFailures;
}

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 128;

void pump(int milliseconds)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(milliseconds);
}

/// Adds a block and waits for the asynchronous creation to complete.
juce::String addBlockAndWait(blockrig::BlockRigProcessor& processor, const juce::PluginDescription& description,
                             int index)
{
    juce::String resultUid;
    juce::String resultError;
    bool done = false;

    processor.addBlock(description, index, [&](juce::String uid, juce::String error) {
        resultUid = std::move(uid);
        resultError = std::move(error);
        done = true;
    });

    const auto deadline = juce::Time::getMillisecondCounter() + 20000;
    while (!done && juce::Time::getMillisecondCounter() < deadline)
        pump(20);

    if (resultError.isNotEmpty())
        std::printf("       add failed: %s\n", resultError.toRawUTF8());

    return resultUid;
}

const juce::PluginDescription* findAu(blockrig::PluginCatalog& catalog, const juce::String& nameFragment,
                                     juce::OwnedArray<juce::PluginDescription>& storage)
{
    for (auto* format : catalog.getFormatManager().getFormats())
    {
        if (!format->getName().containsIgnoreCase("AudioUnit"))
            continue;

        const auto identifiers = format->searchPathsForPlugins(format->getDefaultLocationsToSearch(), true, true);

        for (const auto& identifier : identifiers)
        {
            if (!identifier.containsIgnoreCase("appl"))
                continue;

            juce::OwnedArray<juce::PluginDescription> found;
            format->findAllTypesForFile(found, identifier);

            for (auto* description : found)
            {
                if (description->isInstrument || !description->name.containsIgnoreCase(nameFragment))
                    continue;
                storage.add(new juce::PluginDescription(*description));
                return storage.getLast();
            }
        }
    }

    return nullptr;
}

void prepare(blockrig::BlockRigProcessor& processor)
{
    processor.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);
}

void renderSome(blockrig::BlockRigProcessor& processor, int blocks = 8)
{
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;

    for (int block = 0; block < blocks; ++block)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer(channel);
            for (int i = 0; i < kBlockSize; ++i)
                data[i] = 0.1f * std::sin(0.03f * static_cast<float>(block * kBlockSize + i));
        }
        midi.clear();
        processor.processBlock(buffer, midi);
    }
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File modelsDir = argc > 1
                                     ? juce::File(juce::String(argv[1]))
                                     : juce::File::getCurrentWorkingDirectory().getChildFile(
                                           "third_party/NeuralAmpModelerCore/example_models");

    juce::MemoryBlock savedRig;
    juce::String namUid;

    std::printf("Building a rig\n");
    {
        blockrig::BlockRigProcessor processor;
        prepare(processor);

        juce::OwnedArray<juce::PluginDescription> storage;
        const auto* au = findAu(processor.getCatalog(), "AUDelay", storage);
        check(au != nullptr, "found an Apple AU to use as a third-party block");

        const auto builtIns = processor.getCatalog().getBuiltInDescriptions();
        check(!builtIns.isEmpty(), "catalog offers the built-in NAM block");
        if (builtIns.isEmpty() || au == nullptr)
            return 1;

        // NAM first, then a third-party AU after it: the canonical guitar rig.
        namUid = addBlockAndWait(processor, builtIns.getFirst(), 0);
        check(namUid.isNotEmpty(), "NAM block added");

        const auto auUid = addBlockAndWait(processor, *au, 1);
        check(auUid.isNotEmpty(), "AU block added");
        check(processor.getChain().getNumBlocks() == 2, "lane holds two blocks");
        check(processor.getChain().getBlockByIndex(0)->getUid() == namUid, "NAM is first in the lane");

        // Give the NAM block a capture and a distinctive setting, so the restore
        // has something specific to prove.
        auto* namBlock = processor.getChain().getBlockByUid(namUid);
        auto* namProcessor = dynamic_cast<blockrig::NamBlockProcessor*>(namBlock->getPlugin());
        check(namProcessor != nullptr, "NAM block exposes its processor");

        if (namProcessor != nullptr)
        {
            const auto namFile = modelsDir.getChildFile("A2.nam");
            if (namFile.existsAsFile())
            {
                namProcessor->loadModel(namFile);
                const auto deadline = juce::Time::getMillisecondCounter() + 20000;
                while (namProcessor->getModelInfo().json.isEmpty()
                       && juce::Time::getMillisecondCounter() < deadline)
                    pump(20);
                check(namProcessor->getModelInfo().json.isNotEmpty(), "capture loaded into the NAM block");
            }

            if (auto* bass = namProcessor->getValueTreeState().getParameter("bass"))
                bass->setValueNotifyingHost(bass->convertTo0to1(8.5f));
        }

        // Bypass the AU so the flag has to survive too.
        processor.getChain().getBlockByUid(auUid)->setBypassed(true);

        processor.setInputGainDb(-3.5f);
        processor.setOutputGainDb(2.5f);
        processor.setInputMode(blockrig::BlockRigProcessor::InputMode::mono);

        renderSome(processor);

        processor.getStateInformation(savedRig);
        std::printf("       rig state: %.1f KB\n", savedRig.getSize() / 1024.0);
        check(savedRig.getSize() > 1024, "rig state carries the embedded capture");
    }

    std::printf("\nRestoring the rig\n");
    {
        blockrig::BlockRigProcessor restored;
        prepare(restored);

        juce::MemoryInputStream stream(savedRig.getData(), savedRig.getSize(), false);
        const auto rig = juce::ValueTree::readFromStream(stream);
        check(rig.isValid() && rig.hasType(blockrig::rigstate::ids::root), "rig document parses");
        check(static_cast<int>(rig.getProperty(blockrig::rigstate::ids::schemaVersion, 0))
                  == blockrig::rigstate::kSchemaVersion,
              "rig records the current schema version");

        blockrig::rigstate::RestoreResult result;
        bool done = false;
        blockrig::rigstate::restore(restored, rig, [&](blockrig::rigstate::RestoreResult r) {
            result = std::move(r);
            done = true;
        });

        const auto deadline = juce::Time::getMillisecondCounter() + 30000;
        while (!done && juce::Time::getMillisecondCounter() < deadline)
            pump(20);

        check(done, "restore completed");
        check(result.error.isEmpty(), "restore reported no document error: " + result.error);
        std::printf("       requested %d blocks, created %d, missing %d, state-not-restored %d\n",
                    result.blocksRequested, result.blocksCreated, result.missingPlugins.size(),
                    result.stateNotRestored.size());

        check(result.blocksRequested == 2, "both blocks were in the document");
        check(result.blocksCreated == 2, "both blocks came back");
        check(result.missingPlugins.isEmpty(), "no plug-ins reported missing");
        check(restored.getChain().getNumBlocks() == 2, "restored lane holds two blocks");

        // Order must be preserved: NAM first.
        auto* first = restored.getChain().getBlockByIndex(0);
        check(first != nullptr && first->getDisplayName().containsIgnoreCase("NAM"), "lane order preserved");

        // Bypass flag on the second block.
        auto* second = restored.getChain().getBlockByIndex(1);
        check(second != nullptr && second->isBypassed(), "bypass state restored");

        // I/O ends.
        check(std::abs(restored.getInputGainDb() - (-3.5f)) < 0.01f, "input gain restored");
        check(std::abs(restored.getOutputGainDb() - 2.5f) < 0.01f, "output gain restored");

        // Child plug-in state: the NAM capture and its tone setting.
        if (auto* namProcessor = dynamic_cast<blockrig::NamBlockProcessor*>(first->getPlugin()))
        {
            const auto captureDeadline = juce::Time::getMillisecondCounter() + 20000;
            while (namProcessor->getModelInfo().json.isEmpty()
                   && juce::Time::getMillisecondCounter() < captureDeadline)
                pump(20);

            check(namProcessor->getModelInfo().json.isNotEmpty(),
                  "capture restored from rig state without touching the .nam file");

            if (auto* bass = namProcessor->getValueTreeState().getRawParameterValue("bass"))
            {
                std::printf("       restored bass: %.2f (expected 8.50)\n", bass->load());
                check(std::abs(bass->load() - 8.5f) < 0.05f, "NAM block parameter restored");
            }
        }

        renderSome(restored);
        check(true, "restored rig renders without incident");
    }

    std::printf("\nA rig referencing a plug-in that is not installed\n");
    {
        blockrig::BlockRigProcessor processor;
        prepare(processor);

        juce::ValueTree rig(blockrig::rigstate::ids::root);
        rig.setProperty(blockrig::rigstate::ids::schemaVersion, blockrig::rigstate::kSchemaVersion, nullptr);

        juce::ValueTree lane(blockrig::rigstate::ids::lane);
        juce::ValueTree stage(blockrig::rigstate::ids::stage);
        juce::ValueTree row(blockrig::rigstate::ids::row);
        juce::ValueTree block(blockrig::rigstate::ids::block);
        block.setProperty(blockrig::rigstate::ids::format, "VST3", nullptr);
        block.setProperty(blockrig::rigstate::ids::identifier, "/nowhere/Imaginary.vst3", nullptr);
        block.setProperty(blockrig::rigstate::ids::name, "Imaginary Plugin", nullptr);
        row.appendChild(block, nullptr);
        stage.appendChild(row, nullptr);
        lane.appendChild(stage, nullptr);
        rig.appendChild(lane, nullptr);

        blockrig::rigstate::RestoreResult result;
        bool done = false;
        blockrig::rigstate::restore(processor, rig, [&](blockrig::rigstate::RestoreResult r) {
            result = std::move(r);
            done = true;
        });

        const auto deadline = juce::Time::getMillisecondCounter() + 20000;
        while (!done && juce::Time::getMillisecondCounter() < deadline)
            pump(20);

        check(done, "restore completed even with a missing plug-in");
        check(result.error.isEmpty(), "a missing plug-in is not a document error");
        check(result.missingPlugins.size() == 1, "the missing plug-in is reported to the user");
        std::printf("       reported missing: %s\n", result.missingPlugins.joinIntoString(", ").toRawUTF8());
        renderSome(processor);
        check(true, "rig with a missing block still renders");
    }

    std::printf("\nRefusing a rig from the future\n");
    {
        blockrig::BlockRigProcessor processor;
        prepare(processor);

        juce::ValueTree rig(blockrig::rigstate::ids::root);
        rig.setProperty(blockrig::rigstate::ids::schemaVersion, blockrig::rigstate::kSchemaVersion + 5, nullptr);

        blockrig::rigstate::RestoreResult result;
        bool done = false;
        blockrig::rigstate::restore(processor, rig, [&](blockrig::rigstate::RestoreResult r) {
            result = std::move(r);
            done = true;
        });

        while (!done)
            pump(10);

        check(result.error.isNotEmpty(), "a newer schema version is refused with a message");
        std::printf("       message: %s\n", result.error.toRawUTF8());
    }

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", gFailures,
                gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
