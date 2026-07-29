// Headless checks for the DSP that does not depend on JUCE: model loading,
// block-size independence, resampling latency, and the tone stack.
//
// Run: ./build/tests/dsp_tests <path-to-example_models-dir>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <NAM/get_dsp.h>

#include "dsp/ResamplingNam.h"
#include "dsp/ToneStack.h"

namespace
{
int gFailures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (!condition)
        ++gFailures;
}

std::vector<float> makeTestSignal(int numSamples, double sampleRate, double frequency)
{
    std::vector<float> signal(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        signal[static_cast<size_t>(i)] = static_cast<float>(0.25 * std::sin(2.0 * M_PI * frequency * t));
    }
    return signal;
}

bool allFinite(const std::vector<float>& data)
{
    for (float value : data)
        if (!std::isfinite(value))
            return false;
    return true;
}

double rms(const std::vector<float>& data, int start = 0)
{
    double sum = 0.0;
    int count = 0;
    for (size_t i = static_cast<size_t>(start); i < data.size(); ++i, ++count)
        sum += static_cast<double>(data[i]) * data[i];
    return count > 0 ? std::sqrt(sum / count) : 0.0;
}

std::unique_ptr<nammodeler::ResamplingNam> load(const std::filesystem::path& path)
{
    auto model = nam::get_dsp(path);
    if (model == nullptr)
        return nullptr;
    return std::make_unique<nammodeler::ResamplingNam>(std::move(model));
}

// Runs a signal through a model in fixed-size chunks.
std::vector<float> processInBlocks(nammodeler::ResamplingNam& model, std::vector<float> signal, int blockSize)
{
    int offset = 0;
    const int total = static_cast<int>(signal.size());
    while (offset < total)
    {
        const int chunk = std::min(blockSize, total - offset);
        model.process(signal.data() + offset, chunk);
        offset += chunk;
    }
    return signal;
}

void testModel(const std::filesystem::path& path)
{
    std::printf("\n%s\n", path.filename().string().c_str());

    auto model = load(path);
    if (model == nullptr)
    {
        check(false, "model loaded");
        return;
    }

    constexpr int kBlockSize = 512;
    constexpr double kHostRate = 48000.0;
    model->reset(kHostRate, kBlockSize);

    check(true, "model loaded and prewarmed");
    std::printf("       model rate %.0f Hz, resampling %s, latency %d smp\n", model->getModelSampleRate(),
                model->needToResample() ? "yes" : "no", model->getLatencySamples());

    const auto input = makeTestSignal(24000, kHostRate, 220.0);
    const auto output = processInBlocks(*model, input, kBlockSize);

    check(allFinite(output), "output is finite");
    // Skip the first 2000 samples so the model's startup transient is excluded.
    check(rms(output, 2000) > 1.0e-6, "output is non-silent");

    // Block-size independence: the model is a streaming, causal process, so the
    // result must not depend on how the host chops up the buffer.
    auto fresh64 = load(path);
    auto fresh512 = load(path);
    fresh64->reset(kHostRate, 512);
    fresh512->reset(kHostRate, 512);
    const auto out64 = processInBlocks(*fresh64, input, 64);
    const auto out512 = processInBlocks(*fresh512, input, 512);

    double maxDelta = 0.0;
    for (size_t i = 0; i < out64.size(); ++i)
        maxDelta = std::max(maxDelta, std::abs(static_cast<double>(out64[i]) - out512[i]));
    check(maxDelta < 1.0e-5, "block size 64 matches 512 (max delta " + std::to_string(maxDelta) + ")");

    // Latency policy: none at the model's own rate, non-zero when resampling.
    auto atModelRate = load(path);
    atModelRate->reset(atModelRate->getModelSampleRate(), kBlockSize);
    check(atModelRate->getLatencySamples() == 0, "zero latency at the model's own sample rate");

    auto at44k = load(path);
    at44k->reset(44100.0, kBlockSize);
    const bool resamples = at44k->needToResample();
    std::printf("       at 44.1 kHz: resampling %s, latency %d smp\n", resamples ? "yes" : "no",
                at44k->getLatencySamples());
    check(!resamples || at44k->getLatencySamples() > 0, "resampling reports non-zero latency");

    auto out44k = makeTestSignal(12000, 44100.0, 220.0);
    out44k = processInBlocks(*at44k, out44k, kBlockSize);
    check(allFinite(out44k), "resampled output is finite");
    check(rms(out44k, 2000) > 1.0e-6, "resampled output is non-silent");
}

void testToneStack()
{
    std::printf("\nToneStack\n");

    constexpr double kSampleRate = 48000.0;
    constexpr int kNumSamples = 48000;

    nammodeler::ToneStack stack;
    stack.prepare(kSampleRate);

    // Neutral knobs must be transparent.
    stack.setKnobs(5.0f, 5.0f, 5.0f);
    auto signal = makeTestSignal(kNumSamples, kSampleRate, 440.0);
    const auto reference = signal;
    stack.processBlock(signal.data(), kNumSamples);

    double maxDelta = 0.0;
    for (size_t i = 0; i < signal.size(); ++i)
        maxDelta = std::max(maxDelta, std::abs(static_cast<double>(signal[i]) - reference[i]));
    check(maxDelta < 1.0e-5, "knobs at 5 are transparent (max delta " + std::to_string(maxDelta) + ")");

    // Bass boost should lift a low tone; treble boost should not.
    const auto lowTone = makeTestSignal(kNumSamples, kSampleRate, 80.0);

    nammodeler::ToneStack bassBoost;
    bassBoost.prepare(kSampleRate);
    bassBoost.setKnobs(10.0f, 5.0f, 5.0f);
    auto boosted = lowTone;
    bassBoost.processBlock(boosted.data(), kNumSamples);

    const double referenceRms = rms(lowTone, 4800);
    const double boostedRms = rms(boosted, 4800);
    const double gainDb = 20.0 * std::log10(boostedRms / referenceRms);
    std::printf("       bass at 10 gives %+.1f dB at 80 Hz (expect near +20)\n", gainDb);
    check(gainDb > 15.0, "bass boost lifts 80 Hz");

    nammodeler::ToneStack bassCut;
    bassCut.prepare(kSampleRate);
    bassCut.setKnobs(0.0f, 5.0f, 5.0f);
    auto cut = lowTone;
    bassCut.processBlock(cut.data(), kNumSamples);
    const double cutDb = 20.0 * std::log10(rms(cut, 4800) / referenceRms);
    std::printf("       bass at 0 gives %+.1f dB at 80 Hz (expect near -20)\n", cutDb);
    check(cutDb < -15.0, "bass cut lowers 80 Hz");

    // Treble at 1.8 kHz shelf: a 5 kHz tone should rise about 10 dB.
    const auto highTone = makeTestSignal(kNumSamples, kSampleRate, 5000.0);
    nammodeler::ToneStack trebleBoost;
    trebleBoost.prepare(kSampleRate);
    trebleBoost.setKnobs(5.0f, 5.0f, 10.0f);
    auto high = highTone;
    trebleBoost.processBlock(high.data(), kNumSamples);
    const double trebleDb = 20.0 * std::log10(rms(high, 4800) / rms(highTone, 4800));
    std::printf("       treble at 10 gives %+.1f dB at 5 kHz (expect near +10)\n", trebleDb);
    check(trebleDb > 7.0, "treble boost lifts 5 kHz");
}
} // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path modelsDir =
        argc > 1 ? std::filesystem::path(argv[1])
                 : std::filesystem::path("third_party/NeuralAmpModelerCore/example_models");

    std::printf("Models directory: %s\n", modelsDir.string().c_str());

    testToneStack();

    const char* models[] = {"wavenet.nam", "lstm.nam", "A2.nam", "wavenet_a2_max.nam", "slimmable_container.nam",
                            "wavenet_a1_standard.nam"};

    for (const char* name : models)
    {
        const auto path = modelsDir / name;
        if (std::filesystem::exists(path))
            testModel(path);
        else
            std::printf("\n%s: not found, skipping\n", name);
    }

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", gFailures,
                gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
