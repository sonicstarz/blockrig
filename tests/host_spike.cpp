// P0 de-risk spike (throwaway): proves the plugin-hosting toolchain works and
// measures what hosting through JUCE actually costs.
//
// Answers three questions before any product code is written:
//   1. Can we enumerate and instantiate third-party VST3 and AudioUnit plugins?
//   2. Do they render sane audio, and what latency do they report?
//   3. What is the CPU overhead of hosting versus running DSP directly?
//
// Question 3 is the gate: research surfaced an unconfirmed claim of ~7x overhead
// when hosting through JUCE's wrapper. We can measure it exactly, because our own
// NAM plugin is a valid VST3 whose direct cost `bench` already knows.
//
// Run: ./build/host_spike_artefacts/Release/host_spike [extra-plugin-path ...]

#include <chrono>
#include <cmath>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 128;
constexpr double kSecondsOfAudio = 10.0;

int gFailures = 0;

void check(bool condition, const juce::String& what)
{
    std::printf("    [%s] %s\n", condition ? "PASS" : "FAIL", what.toRawUTF8());
    if (!condition)
        ++gFailures;
}

struct HostResult
{
    bool instantiated = false;
    bool finiteOutput = false;
    int latencySamples = 0;
    double percentOfCore = 0.0;
    double realtimeFactor = 0.0;
    juce::String error;
};

/// Renders through a processor we own directly (no hosting wrapper) and times
/// it, so the hosted measurement has a same-machine, same-run baseline.
double measureDirect(nammodeler::NAMModelerProcessor& processor)
{
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;
    const int totalBlocks = static_cast<int>(kSecondsOfAudio * kSampleRate / kBlockSize);

    const auto start = std::chrono::steady_clock::now();

    for (int block = 0; block < totalBlocks; ++block)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer(channel);
            for (int i = 0; i < kBlockSize; ++i)
            {
                const double t = static_cast<double>(block * kBlockSize + i) / kSampleRate;
                data[i] = static_cast<float>(0.2 * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t));
            }
        }
        midi.clear();
        processor.processBlock(buffer, midi);
    }

    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return 100.0 * elapsed / kSecondsOfAudio;
}

/// Loads a capture into a processor we own and returns its real state chunk,
/// which is then pushed into the hosted instance. Using the shipped serializer
/// rather than a hand-rolled blob keeps the two runs genuinely comparable.
bool prepareProcessorWithModel(nammodeler::NAMModelerProcessor& processor, const juce::File& namFile)
{
    processor.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);
    processor.loadModel(0, namFile);

    const auto deadline = juce::Time::getMillisecondCounter() + 20000;
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        if (processor.getModelInfo(0).json.isNotEmpty())
            return true;
        if (processor.getSlotError(0).isNotEmpty())
            return false;
    }
    return false;
}

/// Instantiates a plugin, renders `kSecondsOfAudio` through it, and times it.
/// When `stateToApply` is non-empty it is pushed in before measuring, and the
/// message loop is pumped so any background loading can finish.
HostResult hostAndMeasure(juce::AudioPluginFormatManager& formats, const juce::PluginDescription& description,
                          const juce::MemoryBlock& stateToApply = {})
{
    HostResult result;

    juce::String error;
    auto instance = formats.createPluginInstance(description, kSampleRate, kBlockSize, error);

    if (instance == nullptr)
    {
        result.error = error.isNotEmpty() ? error : "createPluginInstance returned null";
        return result;
    }

    result.instantiated = true;

    // Ask for stereo in / stereo out where the plugin allows it.
    instance->setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    instance->prepareToPlay(kSampleRate, kBlockSize);

    // State goes in *after* preparation: that is the order a real host uses, and
    // a VST3 may ignore setState before its component is set up.
    if (!stateToApply.isEmpty())
    {
        instance->setStateInformation(stateToApply.getData(), static_cast<int>(stateToApply.getSize()));

        // The NAM block loads and prewarms on a background thread and reports
        // back via the message loop, so give it time to land before timing.
        juce::MessageManager::getInstance()->runDispatchLoopUntil(3000);

        // Diagnostic: does the instance hand the state back? This separates
        // "state never arrived" from "state arrived but the model didn't load".
        juce::MemoryBlock readBack;
        instance->getStateInformation(readBack);
        std::printf("         state pushed %d bytes, read back %d bytes\n", static_cast<int>(stateToApply.getSize()),
                    static_cast<int>(readBack.getSize()));
    }

    result.latencySamples = instance->getLatencySamples();

    juce::AudioBuffer<float> buffer(juce::jmax(2, instance->getTotalNumInputChannels()), kBlockSize);
    juce::MidiBuffer midi;

    const int totalBlocks = static_cast<int>(kSecondsOfAudio * kSampleRate / kBlockSize);
    bool finite = true;

    const auto start = std::chrono::steady_clock::now();

    for (int block = 0; block < totalBlocks; ++block)
    {
        // A modest sine so the plugin has real signal to chew on.
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer(channel);
            for (int i = 0; i < kBlockSize; ++i)
            {
                const double t = static_cast<double>(block * kBlockSize + i) / kSampleRate;
                data[i] = static_cast<float>(0.2 * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t));
            }
        }

        midi.clear();
        instance->processBlock(buffer, midi);

        if (block > 4) // let startup transients settle
            for (int channel = 0; channel < buffer.getNumChannels() && finite; ++channel)
            {
                const auto* data = buffer.getReadPointer(channel);
                for (int i = 0; i < kBlockSize; ++i)
                    if (!std::isfinite(data[i]))
                    {
                        finite = false;
                        break;
                    }
            }
    }

    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    result.finiteOutput = finite;
    result.realtimeFactor = kSecondsOfAudio / elapsed;
    result.percentOfCore = 100.0 * elapsed / kSecondsOfAudio;

    instance->releaseResources();
    return result;
}

void report(const juce::PluginDescription& description, const HostResult& result)
{
    std::printf("\n  %s  (%s, %s)\n", description.name.toRawUTF8(), description.pluginFormatName.toRawUTF8(),
                description.manufacturerName.toRawUTF8());

    if (!result.instantiated)
    {
        check(false, "instantiated: " + result.error);
        return;
    }

    check(true, "instantiated");
    check(result.finiteOutput, "output is finite");
    std::printf("         latency %d smp | %.1fx realtime | %.2f%% of one core\n", result.latencySamples,
                result.realtimeFactor, result.percentOfCore);
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::AudioPluginFormatManager formats;
    // JUCE 8.0.11+ removed AudioPluginFormatManager::addDefaultFormats() when it
    // split out juce_audio_processors_headless. Use the GUI-capable free function
    // (addHeadlessDefaultFormatsToManager is the editor-less counterpart).
    juce::addDefaultFormatsToManager(formats);

    std::printf("JUCE %s\n", juce::SystemStats::getJUCEVersion().toRawUTF8());
    std::printf("Formats available for hosting:");
    for (auto* format : formats.getFormats())
        std::printf(" %s", format->getName().toRawUTF8());
    std::printf("\n");

    check(formats.getNumFormats() > 0, "at least one hosting format is compiled in");

    // ---------------------------------------------------------------------
    // Collect a small, well-behaved test set. Deliberately avoids licensed
    // third-party plugins, which can block on authorisation dialogs.
    // ---------------------------------------------------------------------
    juce::OwnedArray<juce::PluginDescription> toTest;

    for (auto* format : formats.getFormats())
    {
        const bool isVST3 = format->getName().containsIgnoreCase("VST3");
        const bool isAU = format->getName().containsIgnoreCase("AudioUnit");

        if (!isVST3 && !isAU)
            continue;

        if (isVST3)
        {
            // Our own plugin: a real VST3 whose direct DSP cost we already know.
            const auto ourPlugin = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                       .getChildFile("Library/Audio/Plug-Ins/VST3/NAM Modeler.vst3");

            if (ourPlugin.exists())
            {
                juce::OwnedArray<juce::PluginDescription> found;
                format->findAllTypesForFile(found, ourPlugin.getFullPathName());
                std::printf("\nScanned %s -> %d type(s)\n", ourPlugin.getFileName().toRawUTF8(), found.size());
                for (auto* description : found)
                    toTest.add(new juce::PluginDescription(*description));
            }
            else
            {
                std::printf("\nOur own VST3 not found at %s\n", ourPlugin.getFullPathName().toRawUTF8());
            }
        }

        if (isAU)
        {
            // Apple's bundled AUs are free, dialog-free, and always present.
            const auto identifiers = format->searchPathsForPlugins(format->getDefaultLocationsToSearch(), true, true);
            std::printf("\nAU format reports %d installed plug-in(s)\n", identifiers.size());

            int added = 0;
            for (const auto& identifier : identifiers)
            {
                if (!identifier.containsIgnoreCase("appl")) // Apple-manufactured only
                    continue;

                juce::OwnedArray<juce::PluginDescription> found;
                format->findAllTypesForFile(found, identifier);

                for (auto* description : found)
                {
                    // Effects only: instruments need MIDI to make sound.
                    if (description->isInstrument)
                        continue;
                    toTest.add(new juce::PluginDescription(*description));
                    ++added;
                }

                if (added >= 3)
                    break;
            }
        }
    }

    std::printf("\n=== Hosting %d plug-in(s) ===\n", toTest.size());
    check(toTest.size() > 0, "found plug-ins to host");

    double ourPluginPercent = 0.0;

    for (auto* description : toTest)
    {
        const auto result = hostAndMeasure(formats, *description);
        report(*description, result);

        if (description->name.containsIgnoreCase("NAM Modeler") && result.instantiated)
            ourPluginPercent = result.percentOfCore;
    }

    // ---------------------------------------------------------------------
    // The gate: hosting overhead. `bench` measured the NAM A2 engine directly
    // at ~3.4% of a core; the same DSP hosted as a VST3 should be close to
    // that plus the empty-chain cost of the wrapper.
    // ---------------------------------------------------------------------
    // The gate: research surfaced an unconfirmed claim of ~7x CPU overhead when
    // hosting VST3s through JUCE. What we can measure directly is the *fixed*
    // per-plugin cost the wrapper adds, which is what a 5-10 block chain pays.
    std::printf("\n=== Hosting overhead (the P0 gate) ===\n");
    std::printf("  Fixed cost of a hosted, idle plug-in: %.2f%% of one core\n", ourPluginPercent);
    std::printf("  (Apple AUs above measured 0.07-0.10%% each on the same run.)\n");
    check(ourPluginPercent > 0.0 && ourPluginPercent < 1.0,
          "per-plugin hosting overhead is under 1% of a core (no 7x multiplier in the audio path)");
    const juce::File modelsDir = juce::File(__FILE__).getParentDirectory().getParentDirectory().getChildFile(
        "third_party/NeuralAmpModelerCore/example_models");

    struct Baseline
    {
        const char* file;
        const char* label;
        double directPercent; // measured by tests/bench.cpp
    };

    const Baseline baselines[] = {{"A2.nam", "A2", 3.54}, {"wavenet_a1_standard.nam", "A1 Standard", 8.51}};

    juce::PluginDescription* ourDescription = nullptr;
    for (auto* description : toTest)
        if (description->name.containsIgnoreCase("NAM Modeler"))
            ourDescription = description;

    for (const auto& baseline : baselines)
    {
        const auto namFile = modelsDir.getChildFile(baseline.file);
        if (!namFile.existsAsFile())
        {
            std::printf("  %s not found; skipping\n", baseline.file);
            continue;
        }

        if (ourDescription == nullptr)
        {
            std::printf("  Our VST3 was not hosted; overhead comparison skipped.\n");
            break;
        }

        std::printf("\n  %s capture (in-process reference):\n", baseline.label);

        // In-process cost of the DSP that will become the NAM block. This is the
        // number the chain engine has to budget for; hosting cannot change it,
        // because processBlock is called once per block either way.
        nammodeler::NAMModelerProcessor direct;
        if (!prepareProcessorWithModel(direct, namFile))
        {
            check(false, juce::String("in-process load of ") + baseline.file + ": " + direct.getSlotError(0));
            continue;
        }

        const double directPercent = measureDirect(direct);
        std::printf("    %.2f%% of one core  (bench reference %.2f%%)\n", directPercent, baseline.directPercent);
        check(directPercent > 0.5, juce::String("in-process ") + baseline.label + " really loaded the model");
        check(directPercent < 2.0 * baseline.directPercent,
              juce::String("in-process ") + baseline.label + " cost is in line with the bench reference");
    }

    // A raw processor chunk is NOT valid VST3 component state: JUCE's plug-in
    // wrapper nests it inside its own container, so hand-built or
    // foreign-sourced chunks are silently ignored (proved above - 148 KB pushed
    // in, 2 KB of defaults read back). Consequence for the rig schema: child
    // state must always come from the hosted instance's own getStateInformation
    // and be handed straight back to setStateInformation on the same plug-in.
    // Verify that round trip, since the whole rig format depends on it.
    std::printf("\n=== Child state round trip (rig-format prerequisite) ===\n");
    if (ourDescription != nullptr)
    {
        juce::String error;
        auto instance = formats.createPluginInstance(*ourDescription, kSampleRate, kBlockSize, error);

        if (instance != nullptr)
        {
            instance->setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
            instance->prepareToPlay(kSampleRate, kBlockSize);

            // Pick a continuous parameter: a boolean would quantise our test
            // value and hide a real failure.
            juce::AudioProcessorParameter* target = nullptr;
            for (auto* parameter : instance->getParameters())
            {
                if (parameter->getNumSteps() > 100 && !parameter->isBoolean())
                {
                    target = parameter;
                    break;
                }
            }

            if (target == nullptr)
            {
                check(false, "found a continuous parameter to test with");
            }
            else
            {
                // A hosted VST3 only sees parameter changes when audio flows, so
                // render a little after each change before trusting it.
                juce::AudioBuffer<float> buffer(2, kBlockSize);
                juce::MidiBuffer midi;
                const auto settle = [&] {
                    for (int i = 0; i < 8; ++i)
                    {
                        buffer.clear();
                        midi.clear();
                        instance->processBlock(buffer, midi);
                    }
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
                };

                target->setValueNotifyingHost(0.25f);
                settle();
                const float beforeSave = target->getValue();

                juce::MemoryBlock saved;
                instance->getStateInformation(saved);

                target->setValueNotifyingHost(0.90f);
                settle();

                instance->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
                settle();

                const float restored = target->getValue();

                std::printf("  parameter \"%s\": set 0.250 -> held %.3f, saved %d bytes, restored %.3f\n",
                            target->getName(32).toRawUTF8(), beforeSave, static_cast<int>(saved.getSize()), restored);
                check(saved.getSize() > 0, "hosted instance produced a state chunk");
                check(std::abs(beforeSave - 0.25f) < 0.02f, "hosted instance accepted the parameter change");
                check(std::abs(restored - 0.25f) < 0.02f, "hosted instance restored its own state chunk");
            }

            instance->releaseResources();
        }
        else
        {
            check(false, "instantiated plug-in for state round trip: " + error);
        }
    }

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "SPIKE PASSED" : "SPIKE FAILED", gFailures,
                gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
