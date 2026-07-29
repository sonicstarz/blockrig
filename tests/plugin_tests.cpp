// End-to-end checks against the real AudioProcessor: model loading through the
// background loader, stereo routing and panning, solo/mute, output modes, and
// state save/restore with the embedded model.

#include <cmath>
#include <cstdio>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

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
constexpr int kBlockSize = 512;

juce::File modelsDirectory;

/// Pumps the message loop until a slot reports a model (loading happens on a
/// background thread and reports back via callAsync).
bool waitForModel(nammodeler::NAMModelerProcessor& processor, int slotIndex, int timeoutMs = 20000)
{
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        if (processor.getModelInfo(slotIndex).json.isNotEmpty())
            return true;
        if (processor.getSlotError(slotIndex).isNotEmpty())
            return false;
    }
    return false;
}

void setParameter(nammodeler::NAMModelerProcessor& processor, const juce::String& id, float value)
{
    if (auto* parameter = processor.getValueTreeState().getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    else
        check(false, "parameter exists: " + id);
}

void fillSine(juce::AudioBuffer<float>& buffer, double frequency, int startSample = 0)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer(channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double t = static_cast<double>(startSample + i) / kSampleRate;
            data[i] = static_cast<float>(0.2 * std::sin(2.0 * juce::MathConstants<double>::pi * frequency * t));
        }
    }
}

/// Runs several blocks so smoothing settles, then returns per-channel magnitude.
struct Levels
{
    float left = 0.0f;
    float right = 0.0f;
};

Levels render(nammodeler::NAMModelerProcessor& processor, int numBlocks = 8)
{
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;
    Levels levels;

    for (int block = 0; block < numBlocks; ++block)
    {
        fillSine(buffer, 220.0, block * kBlockSize);
        processor.processBlock(buffer, midi);

        // Only measure once smoothing and the model have settled.
        if (block >= numBlocks - 2)
        {
            levels.left = juce::jmax(levels.left, buffer.getMagnitude(0, 0, kBlockSize));
            levels.right = juce::jmax(levels.right, buffer.getMagnitude(1, 0, kBlockSize));
        }
    }

    return levels;
}

void prepare(nammodeler::NAMModelerProcessor& processor)
{
    processor.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);
}

void testLoadingAndRouting()
{
    std::printf("\nLoading and stereo routing\n");

    nammodeler::NAMModelerProcessor processor;
    prepare(processor);

    // With no models loaded the plugin must be silent, not pass dry signal.
    const auto silent = render(processor);
    check(silent.left < 1.0e-6f && silent.right < 1.0e-6f, "silent with no models loaded");

    processor.loadModel(0, modelsDirectory.getChildFile("wavenet.nam"));
    processor.loadModel(1, modelsDirectory.getChildFile("A2.nam"));

    check(waitForModel(processor, 0), "slot A model loaded: " + processor.getSlotError(0));
    check(waitForModel(processor, 1), "slot B model loaded: " + processor.getSlotError(1));

    const auto info = processor.getModelInfo(0);
    check(info.json.isNotEmpty(), "slot A retained the model JSON for state");
    check(info.metrics.modelSampleRate == 48000.0, "slot A model reports 48 kHz");

    // Both amps centred: both channels should carry signal.
    const auto centred = render(processor);
    check(centred.left > 1.0e-4f && centred.right > 1.0e-4f, "both channels produce audio");

    // Hard-pan A left and B right. Muting B must then empty the right channel.
    setParameter(processor, nammodeler::pid::slotParam(0, nammodeler::pid::pan), -1.0f);
    setParameter(processor, nammodeler::pid::slotParam(1, nammodeler::pid::pan), 1.0f);
    setParameter(processor, nammodeler::pid::slotParam(1, nammodeler::pid::mute), 1.0f);

    const auto onlyA = render(processor, 24);
    check(onlyA.left > 1.0e-4f, "amp A alone feeds the left channel");
    check(onlyA.right < 1.0e-4f, "amp A panned hard left leaves the right channel empty");

    // Solo B instead: now only the right channel should sound.
    setParameter(processor, nammodeler::pid::slotParam(1, nammodeler::pid::mute), 0.0f);
    setParameter(processor, nammodeler::pid::slotParam(1, nammodeler::pid::solo), 1.0f);

    const auto onlyB = render(processor, 24);
    check(onlyB.right > 1.0e-4f, "solo on amp B feeds the right channel");
    check(onlyB.left < 1.0e-4f, "solo on amp B silences the left channel");

    setParameter(processor, nammodeler::pid::slotParam(1, nammodeler::pid::solo), 0.0f);

    // Disabling a slot must silence it (and skip the network entirely).
    setParameter(processor, nammodeler::pid::slotParam(0, nammodeler::pid::enabled), 0.0f);
    setParameter(processor, nammodeler::pid::slotParam(1, nammodeler::pid::enabled), 0.0f);
    const auto disabled = render(processor, 24);
    check(disabled.left < 1.0e-6f && disabled.right < 1.0e-6f, "disabled slots are silent");
}

void testOutputModesAndLatency()
{
    std::printf("\nOutput modes and latency\n");

    nammodeler::NAMModelerProcessor processor;
    prepare(processor);
    processor.loadModel(0, modelsDirectory.getChildFile("wavenet.nam"));
    check(waitForModel(processor, 0), "model loaded");

    setParameter(processor, nammodeler::pid::slotParam(1, nammodeler::pid::enabled), 0.0f);

    check(processor.getLatencySamples() == 0, "no reported latency at 48 kHz");

    const auto info = processor.getModelInfo(0);

    // Raw versus Normalized must differ by exactly the loudness offset.
    setParameter(processor, nammodeler::pid::slotParam(0, nammodeler::pid::outMode),
                 static_cast<float>(nammodeler::OutputMode::raw));
    const auto raw = render(processor, 24);

    setParameter(processor, nammodeler::pid::slotParam(0, nammodeler::pid::outMode),
                 static_cast<float>(nammodeler::OutputMode::normalized));
    const auto normalized = render(processor, 24);

    if (info.metrics.hasLoudness && raw.left > 1.0e-6f)
    {
        const double expectedDb = -18.0 - info.metrics.loudness;
        const double measuredDb = 20.0 * std::log10(normalized.left / raw.left);
        std::printf("       loudness %.2f dB -> expected %+.2f dB, measured %+.2f dB\n", info.metrics.loudness,
                    expectedDb, measuredDb);
        check(std::abs(measuredDb - expectedDb) < 0.5, "Normalized applies the loudness offset");
    }
    else
    {
        std::printf("       model has no loudness metadata; skipping offset check\n");
    }

    // A model at 48 kHz played at 44.1 kHz must report the resampler's latency.
    processor.setPlayConfigDetails(2, 2, 44100.0, kBlockSize);
    processor.prepareToPlay(44100.0, kBlockSize);
    processor.loadModel(0, modelsDirectory.getChildFile("wavenet.nam"));
    check(waitForModel(processor, 0), "model reloaded at 44.1 kHz");

    // Let the async latency update land.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
    std::printf("       reported latency at 44.1 kHz: %d samples\n", processor.getLatencySamples());
    check(processor.getLatencySamples() > 0, "latency reported when resampling");
}

void testStateRoundTrip()
{
    std::printf("\nState save and restore\n");

    juce::MemoryBlock state;
    float referenceLevel = 0.0f;

    {
        nammodeler::NAMModelerProcessor processor;
        prepare(processor);
        processor.loadModel(0, modelsDirectory.getChildFile("wavenet.nam"));
        check(waitForModel(processor, 0), "model loaded before save");

        setParameter(processor, nammodeler::pid::slotParam(1, nammodeler::pid::enabled), 0.0f);
        setParameter(processor, nammodeler::pid::slotParam(0, nammodeler::pid::bass), 8.0f);
        setParameter(processor, nammodeler::pid::slotParam(0, nammodeler::pid::outTrim), -6.0f);

        referenceLevel = render(processor, 24).left;
        processor.getStateInformation(state);
    }

    std::printf("       state size: %.1f KB\n", state.getSize() / 1024.0);
    check(state.getSize() > 1024, "state contains the embedded model");

    nammodeler::NAMModelerProcessor restored;
    prepare(restored);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    check(waitForModel(restored, 0), "model restored from embedded state (no file access)");

    const auto bass = restored.getValueTreeState().getRawParameterValue(
        nammodeler::pid::slotParam(0, nammodeler::pid::bass));
    check(bass != nullptr && std::abs(bass->load() - 8.0f) < 0.01f, "parameters restored");

    const float restoredLevel = render(restored, 24).left;
    const double deltaDb =
        (referenceLevel > 1.0e-6f && restoredLevel > 1.0e-6f) ? 20.0 * std::log10(restoredLevel / referenceLevel) : 99.0;
    std::printf("       level before %.5f, after %.5f (%.2f dB)\n", referenceLevel, restoredLevel, deltaDb);
    check(std::abs(deltaDb) < 0.1, "restored session renders at the same level");
}

void testModelSwapUnderAudio()
{
    std::printf("\nModel swapping while audio runs\n");

    nammodeler::NAMModelerProcessor processor;
    prepare(processor);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;

    const char* models[] = {"wavenet.nam", "A2.nam", "lstm.nam", "wavenet_a2_max.nam"};
    bool everFinite = true;

    // Hammer the loader while rendering continuously: this is what exercises the
    // staging/retirement handover.
    for (int round = 0; round < 12; ++round)
    {
        processor.loadModel(0, modelsDirectory.getChildFile(models[round % 4]));
        processor.loadModel(1, modelsDirectory.getChildFile(models[(round + 1) % 4]));

        for (int block = 0; block < 20; ++block)
        {
            fillSine(buffer, 220.0, block * kBlockSize);
            processor.processBlock(buffer, midi);

            for (int channel = 0; channel < 2; ++channel)
            {
                const auto* data = buffer.getReadPointer(channel);
                for (int i = 0; i < kBlockSize; ++i)
                    if (!std::isfinite(data[i]) || std::abs(data[i]) > 100.0f)
                        everFinite = false;
            }
        }

        juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    }

    check(everFinite, "output stays finite and bounded across rapid model swaps");
    check(processor.getModelInfo(0).json.isNotEmpty(), "a model is still loaded after the swap storm");
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    modelsDirectory = argc > 1 ? juce::File(juce::String(argv[1]))
                               : juce::File::getCurrentWorkingDirectory().getChildFile(
                                     "third_party/NeuralAmpModelerCore/example_models");

    std::printf("Models directory: %s\n", modelsDirectory.getFullPathName().toRawUTF8());

    if (!modelsDirectory.isDirectory())
    {
        std::printf("Models directory not found\n");
        return 1;
    }

    testLoadingAndRouting();
    testOutputModesAndLatency();
    testStateRoundTrip();
    testModelSwapUnderAudio();

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", gFailures,
                gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
