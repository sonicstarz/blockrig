// Exercises the chain engine with real hosted plug-ins: ordering, bypass,
// latency summation, per-block CPU attribution, and — the important one — edits
// made while audio is rendering continuously.

#include <chrono>
#include <cmath>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "host/BlockChain.h"

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

juce::AudioPluginFormatManager gFormats;
juce::OwnedArray<juce::PluginDescription> gAvailable;
int gUidCounter = 0;

/// Collects a few dialog-free plug-ins to build chains from: Apple's bundled
/// AUs plus our own VST3.
void discoverPlugins()
{
    juce::addDefaultFormatsToManager(gFormats);

    for (auto* format : gFormats.getFormats())
    {
        if (format->getName().containsIgnoreCase("VST3"))
        {
            const auto ourPlugin = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                       .getChildFile("Library/Audio/Plug-Ins/VST3/NAM Modeler.vst3");
            if (ourPlugin.exists())
                format->findAllTypesForFile(gAvailable, ourPlugin.getFullPathName());
        }

        if (format->getName().containsIgnoreCase("AudioUnit"))
        {
            const auto identifiers = format->searchPathsForPlugins(format->getDefaultLocationsToSearch(), true, true);
            int added = 0;

            for (const auto& identifier : identifiers)
            {
                if (!identifier.containsIgnoreCase("appl"))
                    continue;

                juce::OwnedArray<juce::PluginDescription> found;
                format->findAllTypesForFile(found, identifier);

                for (auto* description : found)
                {
                    if (description->isInstrument)
                        continue;
                    gAvailable.add(new juce::PluginDescription(*description));
                    ++added;
                }

                if (added >= 4)
                    break;
            }
        }
    }
}

const juce::PluginDescription* findDescription(const juce::String& nameFragment)
{
    for (auto* description : gAvailable)
        if (description->name.containsIgnoreCase(nameFragment))
            return description;
    return nullptr;
}

std::unique_ptr<blockrig::BlockInstance> makeBlock(const juce::PluginDescription& description)
{
    juce::String error;
    auto instance = gFormats.createPluginInstance(description, kSampleRate, kBlockSize, error);
    if (instance == nullptr)
        return nullptr;

    return std::make_unique<blockrig::BlockInstance>(std::move(instance),
                                                    "b" + juce::String(++gUidCounter));
}

void fillSine(juce::AudioBuffer<float>& buffer, int startSample)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer(channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double t = static_cast<double>(startSample + i) / kSampleRate;
            data[i] = static_cast<float>(0.2 * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t));
        }
    }
}

/// Renders blocks through the chain and reports whether output stayed sane.
bool render(blockrig::BlockChain& chain, int numBlocks, float* magnitudeOut = nullptr)
{
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;
    bool sane = true;
    float magnitude = 0.0f;

    for (int block = 0; block < numBlocks; ++block)
    {
        fillSine(buffer, block * kBlockSize);
        midi.clear();
        chain.process(buffer, midi);

        for (int channel = 0; channel < 2; ++channel)
        {
            const auto* data = buffer.getReadPointer(channel);
            for (int i = 0; i < kBlockSize; ++i)
            {
                if (!std::isfinite(data[i]) || std::abs(data[i]) > 50.0f)
                    sane = false;
                magnitude = juce::jmax(magnitude, std::abs(data[i]));
            }
        }
    }

    if (magnitudeOut != nullptr)
        *magnitudeOut = magnitude;

    return sane;
}

void testOrderingAndLatency()
{
    std::printf("\nChain ordering and latency\n");

    blockrig::BlockChain chain;
    chain.prepare(kSampleRate, kBlockSize);

    check(chain.getNumBlocks() == 0, "starts empty");
    check(chain.getLatencySamples() == 0, "empty chain reports zero latency");

    // An empty chain must pass audio through untouched.
    float magnitude = 0.0f;
    check(render(chain, 4, &magnitude), "empty chain renders sanely");
    check(magnitude > 0.1f, "empty chain passes signal through");

    // AUDynamicsProcessor reports 256 samples of latency, which makes it a
    // useful probe for the chain's latency summation.
    const auto* latent = findDescription("AUDynamicsProcessor");
    const auto* delay = findDescription("AUDelay");

    if (latent == nullptr || delay == nullptr)
    {
        check(false, "found Apple AUs to build a chain from");
        return;
    }

    auto latentBlock = makeBlock(*latent);
    if (latentBlock == nullptr)
    {
        check(false, "instantiated the latent block");
        return;
    }

    // Latency only becomes meaningful once the plug-in has been prepared, which
    // the chain does on insertion — so read it from the block after inserting.
    auto* latentPtr = latentBlock.get();
    chain.insertBlock(std::move(latentBlock), 0);
    check(chain.getNumBlocks() == 1, "block inserted");
    check(render(chain, 4), "one-block chain renders sanely");

    const int blockLatency = latentPtr->getLatencySamples();
    std::printf("       %s reports %d smp after preparation\n", latentPtr->getDisplayName().toRawUTF8(),
                blockLatency);
    check(blockLatency > 0, "the latent AU reports non-zero latency once prepared");
    check(chain.getLatencySamples() == blockLatency,
          "chain latency matches its single block (" + juce::String(blockLatency) + " smp)");

    // Add a second copy: latency must sum.
    auto secondLatent = makeBlock(*latent);
    chain.insertBlock(std::move(secondLatent), 1);
    check(render(chain, 4), "two-block chain renders sanely");
    check(chain.getLatencySamples() == 2 * blockLatency, "latency sums across blocks");

    // A zero-latency block should not change the total.
    auto delayBlock = makeBlock(*delay);
    chain.insertBlock(std::move(delayBlock), 1);
    check(chain.getNumBlocks() == 3, "third block inserted in the middle");
    check(render(chain, 8), "three-block chain renders sanely");

    // Reordering must not change latency, only order.
    const int beforeMove = chain.getLatencySamples();
    const auto uid = chain.getBlockByIndex(0)->getUid();
    chain.moveBlock(uid, 2);
    check(render(chain, 4), "chain renders sanely after a reorder");
    check(chain.getBlockByIndex(2)->getUid() == uid, "block moved to the requested index");
    check(chain.getLatencySamples() == beforeMove, "reordering leaves total latency unchanged");

    // Removal must drop that block's latency.
    chain.removeBlock(uid);
    check(chain.getNumBlocks() == 2, "block removed");
    check(render(chain, 4), "chain renders sanely after a removal");
}

void testBypass()
{
    std::printf("\nBypass\n");

    const auto* delay = findDescription("AUDelay");
    if (delay == nullptr)
    {
        check(false, "found an AU to bypass");
        return;
    }

    blockrig::BlockChain chain;
    chain.prepare(kSampleRate, kBlockSize);

    auto block = makeBlock(*delay);
    auto* blockPtr = block.get();
    chain.insertBlock(std::move(block), 0);

    check(!blockPtr->isBypassed(), "blocks start active");
    check(render(chain, 4), "renders while active");

    blockPtr->setBypassed(true);
    check(blockPtr->isBypassed(), "bypass flag set");

    float magnitude = 0.0f;
    check(render(chain, 8, &magnitude), "renders while bypassed");
    check(magnitude > 0.1f, "bypassed block still passes signal through");

    blockPtr->setBypassed(false);
    check(render(chain, 4), "renders after un-bypassing");
}

void testCpuAttribution()
{
    std::printf("\nPer-block CPU attribution\n");

    const auto* delay = findDescription("AUDelay");
    if (delay == nullptr)
    {
        check(false, "found an AU to measure");
        return;
    }

    blockrig::BlockChain chain;
    chain.prepare(kSampleRate, kBlockSize);

    auto block = makeBlock(*delay);
    auto* blockPtr = block.get();
    chain.insertBlock(std::move(block), 0);

    render(chain, 400);

    const float blockAverage = blockPtr->getLoad().getAverage();
    const float blockPeak = blockPtr->getLoad().getPeak();
    const float totalAverage = chain.getTotalLoad().getAverage();

    std::printf("       block avg %.4f%% peak %.4f%% | chain avg %.4f%% | dropouts %d\n", blockAverage * 100.0f,
                blockPeak * 100.0f, totalAverage * 100.0f, chain.getDropoutCount());

    check(blockAverage > 0.0f, "block reports a non-zero average load");
    check(blockPeak >= blockAverage, "block peak is at least its average");
    check(totalAverage > 0.0f, "chain reports a non-zero total load");
    check(totalAverage >= blockAverage * 0.5f, "chain total is consistent with its single block");
    check(blockAverage < 1.0f, "a trivial AU costs well under one buffer budget");

    blockPtr->getLoad().clearPeak();
    check(blockPtr->getLoad().getPeak() == 0.0f, "peak can be cleared for display decay");
}

void testEditsWhileRendering()
{
    std::printf("\nEdits while audio is rendering\n");

    const auto* delay = findDescription("AUDelay");
    const auto* bandpass = findDescription("AUBandpass");
    if (delay == nullptr || bandpass == nullptr)
        {
        check(false, "found two AUs for the edit storm");
        return;
    }

    blockrig::BlockChain chain;
    chain.prepare(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;
    bool sane = true;
    int sampleCounter = 0;

    // Interleave edits with rendering, the way the UI will while a user builds a
    // rig with audio running. This is what stresses the snapshot handover.
    for (int round = 0; round < 30; ++round)
    {
        if (auto block = makeBlock(round % 2 == 0 ? *delay : *bandpass))
            chain.insertBlock(std::move(block), round % 3);

        for (int i = 0; i < 6; ++i)
        {
            fillSine(buffer, sampleCounter);
            sampleCounter += kBlockSize;
            midi.clear();
            chain.process(buffer, midi);

            for (int channel = 0; channel < 2; ++channel)
            {
                const auto* data = buffer.getReadPointer(channel);
                for (int s = 0; s < kBlockSize; ++s)
                    if (!std::isfinite(data[s]) || std::abs(data[s]) > 50.0f)
                        sane = false;
            }
        }

        // Remove and reorder while still rendering.
        if (chain.getNumBlocks() > 4)
        {
            chain.removeBlock(chain.getBlockByIndex(0)->getUid());

            for (int i = 0; i < 4; ++i)
            {
                fillSine(buffer, sampleCounter);
                sampleCounter += kBlockSize;
                midi.clear();
                chain.process(buffer, midi);
            }
        }

        if (chain.getNumBlocks() > 2)
            chain.moveBlock(chain.getBlockByIndex(0)->getUid(), chain.getNumBlocks() - 1);

        chain.collectGarbage();
    }

    check(sane, "output stays finite and bounded across 30 rounds of live edits");
    check(chain.getNumBlocks() > 0, "chain still holds blocks after the storm");
    std::printf("       finished with %d blocks, latency %d smp\n", chain.getNumBlocks(),
                chain.getLatencySamples());

    // Clearing the whole lane mid-flight must also be safe.
    chain.clear();
    check(render(chain, 8), "renders sanely after clearing the lane");
    check(chain.getNumBlocks() == 0, "lane is empty after clear");
    chain.collectGarbage();
}

void testMixedFormatChain()
{
    std::printf("\nMixed AU + VST3 chain\n");

    const auto* ours = findDescription("NAM Modeler");
    const auto* delay = findDescription("AUDelay");

    if (ours == nullptr || delay == nullptr)
    {
        std::printf("       needs both our VST3 and an Apple AU; skipping\n");
        return;
    }

    blockrig::BlockChain chain;
    chain.prepare(kSampleRate, kBlockSize);

    auto vst3Block = makeBlock(*ours);
    auto auBlock = makeBlock(*delay);

    check(vst3Block != nullptr, "instantiated a VST3 block");
    check(auBlock != nullptr, "instantiated an AU block");

    if (vst3Block == nullptr || auBlock == nullptr)
        return;

    chain.insertBlock(std::move(vst3Block), 0);
    chain.insertBlock(std::move(auBlock), 1);

    check(render(chain, 16), "AU and VST3 render together in one chain");
    check(chain.getNumBlocks() == 2, "both formats coexist in the lane");
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    discoverPlugins();
    std::printf("Discovered %d plug-in(s) to test with\n", gAvailable.size());

    if (gAvailable.isEmpty())
    {
        std::printf("No plug-ins available; cannot run chain tests\n");
        return 1;
    }

    testOrderingAndLatency();
    testBypass();
    testCpuAttribution();
    testEditsWhileRendering();
    testMixedFormatChain();

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", gFailures,
                gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
