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

bool MidiEngine::isReceivingClock() const
{
    return juce::Time::currentTimeMillis() - mLastClockMs.load(std::memory_order_relaxed) < 500;
}

MidiEngine::Activity MidiEngine::getLastActivity() const
{
    return {mActivityCc.load(std::memory_order_relaxed), mActivityValue.load(std::memory_order_relaxed),
            mActivityChannel.load(std::memory_order_relaxed),
            mActivityMs.load(std::memory_order_relaxed)};
}

void MidiEngine::handleClock()
{
    // 24 pulses per quarter note. Averaging a full beat's worth of intervals
    // rides out the jitter every MIDI source has; a per-pulse estimate would
    // make the tempo readout dance.
    const auto now = juce::Time::getHighResolutionTicks();

    if (mLastPulseTicks != 0)
    {
        const auto seconds = juce::Time::highResolutionTicksToSeconds(now - mLastPulseTicks);

        if (seconds > 0.0005 && seconds < 0.2) // 12.5 to 5000 bpm: plausible pulses only
        {
            mClockIntervals[mClockPulseCount % 24] = seconds;
            ++mClockPulseCount;

            if (mClockPulseCount >= 24)
            {
                double total = 0.0;
                for (const auto interval : mClockIntervals)
                    total += interval;

                const auto bpm = 60.0 / (total / 24.0 * 24.0);
                mClockBpm.store(bpm, std::memory_order_relaxed);

                if (mFollowClock.load(std::memory_order_relaxed))
                    mProcessor.getTransport().setBpm(bpm);
            }
        }
    }

    mLastPulseTicks = now;
    mLastClockMs.store(juce::Time::currentTimeMillis(), std::memory_order_relaxed);
}

void MidiEngine::handle(const juce::MidiMessage& message)
{
    if (message.isMidiClock())
    {
        handleClock();
        return;
    }

    // Program change recalls the snapshot with that number, or the rig, per the
    // configured target. Structural, so it goes to the message thread; the UI
    // notices activeIndex move and catches up.
    if (message.isProgramChange())
    {
        const auto program = message.getProgramChangeNumber();

        if (getProgramTarget() == ProgramTarget::rigs)
        {
            juce::MessageManager::callAsync([this, program] {
                if (onRigProgramRequested)
                    onRigProgramRequested(program);
            });
            return;
        }

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
    const auto channel = message.getChannel();

    mActivityCc.store(cc, std::memory_order_relaxed);
    mActivityValue.store(value, std::memory_order_relaxed);
    mActivityChannel.store(channel, std::memory_order_relaxed);
    mActivityMs.store(juce::Time::currentTimeMillis(), std::memory_order_relaxed);

    // Learn wins over dispatch: the wiggle that teaches a mapping must not also
    // fire whatever that CC used to do.
    if (const auto armed = mLearnIndex.exchange(-1, std::memory_order_relaxed); armed >= 0)
    {
        {
            const juce::ScopedLock lock(mLock);
            if (armed < static_cast<int>(mMappings.size()))
            {
                // Learn the channel too: a controller that sends on channel 3
                // should not be answered by a different pedal on channel 1.
                mMappings[static_cast<size_t>(armed)].cc = cc;
                mMappings[static_cast<size_t>(armed)].channel = channel;
            }
        }

        if (onMappingsChanged)
            juce::MessageManager::callAsync([this] {
                if (onMappingsChanged)
                    onMappingsChanged();
            });
        return;
    }

    dispatch(cc, value, channel);
}

void MidiEngine::dispatch(int cc, int value, int channel)
{
    std::vector<Mapping> matches;
    {
        const juce::ScopedLock lock(mLock);
        for (const auto& mapping : mMappings)
            if (mapping.cc == cc && (mapping.channel == 0 || mapping.channel == channel))
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
            // The pedal's travel maps onto the mapping's range, so an expression
            // pedal can sweep just the part of a parameter that matters.
            const auto normalised = static_cast<float>(value) / 127.0f;
            const auto scaled = mapping.minimum + normalised * (mapping.maximum - mapping.minimum);
            parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, scaled));
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

    if (id == "metronome")
    {
        juce::MessageManager::callAsync([this] {
            auto& transport = mProcessor.getTransport();
            transport.setMetronomeEnabled(!transport.isMetronomeEnabled());
        });
    }
    else if (id == "tap")
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
        entry.setProperty("channel", mapping.channel, nullptr);
        entry.setProperty("minimum", mapping.minimum, nullptr);
        entry.setProperty("maximum", mapping.maximum, nullptr);
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
        mapping.channel = entry.getProperty("channel", 0);
        mapping.minimum = entry.getProperty("minimum", 0.0f);
        mapping.maximum = entry.getProperty("maximum", 1.0f);
        mapping.description = entry.getProperty("description", "").toString();
        mappings.push_back(std::move(mapping));
    }

    setMappings(std::move(mappings));
}

} // namespace blockrig
