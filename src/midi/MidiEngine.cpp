#include "midi/MidiEngine.h"

#include "BlockRigProcessor.h"

namespace blockrig
{

MidiEngine::MidiEngine(BlockRigProcessor& processor)
    : mProcessor(processor)
{
}

std::vector<MidiEngine::Mapping> MidiEngine::getMappings() const
{
    const juce::ScopedLock lock(mLock);
    return mMappings;
}

void MidiEngine::setMappings(std::vector<Mapping> mappings)
{
    {
        const juce::ScopedLock lock(mLock);
        mMappings = std::move(mappings);
    }
    mLearnIndex.store(-1, std::memory_order_relaxed);

    if (onMappingsChanged)
        onMappingsChanged();
}

void MidiEngine::addMapping(Mapping mapping)
{
    int index = 0;
    {
        const juce::ScopedLock lock(mLock);
        mMappings.push_back(std::move(mapping));
        index = static_cast<int>(mMappings.size()) - 1;
    }

    // A new mapping is armed immediately: add, wiggle the pedal, done.
    armLearn(index);

    if (onMappingsChanged)
        onMappingsChanged();
}

void MidiEngine::removeMapping(int index)
{
    {
        const juce::ScopedLock lock(mLock);
        if (index < 0 || index >= static_cast<int>(mMappings.size()))
            return;
        mMappings.erase(mMappings.begin() + index);
    }
    mLearnIndex.store(-1, std::memory_order_relaxed);

    if (onMappingsChanged)
        onMappingsChanged();
}

void MidiEngine::armLearn(int mappingIndex)
{
    mLearnIndex.store(mappingIndex, std::memory_order_relaxed);
}

void MidiEngine::processBuffer(const juce::MidiBuffer& buffer)
{
    for (const auto metadata : buffer)
        handle(metadata.getMessage());
}

void MidiEngine::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    handle(message);
}

void MidiEngine::handle(const juce::MidiMessage& message)
{
    // Program change recalls the snapshot with that number. Structural, so it
    // goes to the message thread; the UI notices activeIndex move and catches up.
    if (message.isProgramChange())
    {
        const auto program = message.getProgramChangeNumber();

        juce::MessageManager::callAsync([this, program] {
            auto& bank = mProcessor.getSnapshots();

            if (program < static_cast<int>(bank.getSnapshots().size()))
            {
                snapshots::Bank::apply(mProcessor,
                                       bank.getSnapshots()[static_cast<size_t>(program)]);
                bank.activeIndex = program;

                const auto& snapshot = bank.getSnapshots()[static_cast<size_t>(program)];
                if (snapshot.includeTuner)
                    mProcessor.setTunerActive(snapshot.tunerActive);
            }
        });
        return;
    }

    if (!message.isController())
        return;

    const auto cc = message.getControllerNumber();
    const auto value = message.getControllerValue();

    // Learn wins over dispatch: the wiggle that teaches a mapping must not also
    // fire whatever that CC used to do.
    if (const auto armed = mLearnIndex.exchange(-1, std::memory_order_relaxed); armed >= 0)
    {
        {
            const juce::ScopedLock lock(mLock);
            if (armed < static_cast<int>(mMappings.size()))
                mMappings[static_cast<size_t>(armed)].cc = cc;
        }

        if (onMappingsChanged)
            juce::MessageManager::callAsync([this] {
                if (onMappingsChanged)
                    onMappingsChanged();
            });
        return;
    }

    dispatch(cc, value);
}

void MidiEngine::dispatch(int cc, int value)
{
    std::vector<Mapping> matches;
    {
        const juce::ScopedLock lock(mLock);
        for (const auto& mapping : mMappings)
            if (mapping.cc == cc)
                matches.push_back(mapping);
    }

    const bool pressed = value >= 64;

    for (const auto& mapping : matches)
    {
        if (mapping.globalId.isNotEmpty())
        {
            applyGlobal(mapping, value, pressed);
            continue;
        }

        auto* block = mProcessor.getChain().getBlockByUid(mapping.blockUid);
        if (block == nullptr)
            continue;

        auto* plugin = block->getPlugin();
        if (plugin == nullptr)
            return;

        const auto& parameters = plugin->getParameters();
        if (mapping.parameterIndex < 0 || mapping.parameterIndex >= parameters.size())
            continue;

        auto* parameter = parameters[mapping.parameterIndex];

        if (mapping.toggle)
        {
            // Momentary switch: only the press flips, the release is ignored.
            if (pressed)
                parameter->setValueNotifyingHost(parameter->getValue() >= 0.5f ? 0.0f : 1.0f);
        }
        else
        {
            parameter->setValueNotifyingHost(static_cast<float>(value) / 127.0f);
        }
    }
}

void MidiEngine::applyGlobal(const Mapping& mapping, int value, bool pressed)
{
    const auto& id = mapping.globalId;

    // Continuous globals apply in place; everything else is an edge-triggered
    // action on the message thread.
    if (id == "mute")
    {
        if (mapping.toggle ? pressed : true)
        {
            const bool target = mapping.toggle ? !mProcessor.isMuted() : value < 64;
            juce::MessageManager::callAsync([this, target] { mProcessor.setMuted(target); });
        }
        return;
    }

    if (!pressed)
        return; // actions fire on press only

    if (id == "tap")
    {
        juce::MessageManager::callAsync([this] { mProcessor.getTransport().tap(); });
    }
    else if (id == "tuner")
    {
        juce::MessageManager::callAsync(
            [this] { mProcessor.setTunerActive(!mProcessor.isTunerActive()); });
    }
    else if (id == "snapshotNext" || id == "snapshotPrev")
    {
        const int direction = id == "snapshotNext" ? 1 : -1;

        juce::MessageManager::callAsync([this, direction] {
            auto& bank = mProcessor.getSnapshots();
            const auto count = static_cast<int>(bank.getSnapshots().size());
            if (count == 0)
                return;

            const auto index = ((bank.activeIndex < 0 ? 0 : bank.activeIndex + direction) + count)
                               % count;
            snapshots::Bank::apply(mProcessor, bank.getSnapshots()[static_cast<size_t>(index)]);
            bank.activeIndex = index;
        });
    }
    else if (id == "rigNext" || id == "rigPrev")
    {
        const int direction = id == "rigNext" ? 1 : -1;

        juce::MessageManager::callAsync([this, direction] {
            if (onRigStepRequested)
                onRigStepRequested(direction);
        });
    }
}

juce::ValueTree MidiEngine::toValueTree() const
{
    juce::ValueTree tree("MidiMappings");

    for (const auto& mapping : getMappings())
    {
        juce::ValueTree entry("Mapping");
        entry.setProperty("cc", mapping.cc, nullptr);
        entry.setProperty("blockUid", mapping.blockUid, nullptr);
        entry.setProperty("parameterIndex", mapping.parameterIndex, nullptr);
        entry.setProperty("globalId", mapping.globalId, nullptr);
        entry.setProperty("toggle", mapping.toggle, nullptr);
        entry.setProperty("description", mapping.description, nullptr);
        tree.appendChild(entry, nullptr);
    }

    return tree;
}

void MidiEngine::restoreFrom(const juce::ValueTree& tree)
{
    std::vector<Mapping> mappings;

    for (const auto& entry : tree)
    {
        if (!entry.hasType("Mapping"))
            continue;

        Mapping mapping;
        mapping.cc = entry.getProperty("cc", -1);
        mapping.blockUid = entry.getProperty("blockUid", "").toString();
        mapping.parameterIndex = entry.getProperty("parameterIndex", -1);
        mapping.globalId = entry.getProperty("globalId", "").toString();
        mapping.toggle = entry.getProperty("toggle", false);
        mapping.description = entry.getProperty("description", "").toString();
        mappings.push_back(std::move(mapping));
    }

    setMappings(std::move(mappings));
}

} // namespace blockrig
