// Measures the real-time factor of each model through the actual plugin signal
// path, so the CPU budget for running two slots is based on this machine rather
// than on published figures.
//
// Run: ./build/bench <path-to-example_models-dir> [blockSize]

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>

#include <NAM/get_dsp.h>

#include "dsp/ResamplingNam.h"
#include "dsp/ToneStack.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kSecondsOfAudio = 20.0;

double benchmark(const std::filesystem::path& path, int blockSize, bool withToneStack)
{
    auto dsp = nam::get_dsp(path);
    if (dsp == nullptr)
        return -1.0;

    nammodeler::ResamplingNam model(std::move(dsp));
    model.reset(kSampleRate, blockSize);

    nammodeler::ToneStack toneStack;
    toneStack.prepare(kSampleRate);
    toneStack.setKnobs(6.0f, 4.0f, 7.0f);

    const int totalSamples = static_cast<int>(kSecondsOfAudio * kSampleRate);
    std::vector<float> buffer(static_cast<size_t>(blockSize));
    for (int i = 0; i < blockSize; ++i)
        buffer[static_cast<size_t>(i)] = 0.1f * std::sin(0.05 * i);

    const auto start = std::chrono::steady_clock::now();

    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        model.process(buffer.data(), blockSize);
        if (withToneStack)
            toneStack.processBlock(buffer.data(), blockSize);
    }

    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return kSecondsOfAudio / elapsed; // real-time factor
}
} // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path modelsDir =
        argc > 1 ? std::filesystem::path(argv[1])
                 : std::filesystem::path("third_party/NeuralAmpModelerCore/example_models");
    const int blockSize = argc > 2 ? std::atoi(argv[2]) : 128;

    std::printf("Block size %d, %.0f Hz, %.0f s of audio per model\n\n", blockSize, kSampleRate, kSecondsOfAudio);
    std::printf("%-28s %12s %10s %14s\n", "model", "realtime x", "1 slot %", "2 slots %");
    std::printf("%-28s %12s %10s %14s\n", "----------------------------", "------------", "----------",
                "--------------");

    const char* models[] = {"wavenet_a1_standard.nam", "wavenet.nam", "A2.nam", "wavenet_a2_max.nam",
                            "slimmable_container.nam", "lstm.nam"};

    for (const char* name : models)
    {
        const auto path = modelsDir / name;
        if (!std::filesystem::exists(path))
            continue;

        const double factor = benchmark(path, blockSize, true);
        if (factor <= 0.0)
        {
            std::printf("%-28s %12s\n", name, "FAILED");
            continue;
        }

        const double onePercent = 100.0 / factor;
        std::printf("%-28s %12.1f %9.2f%% %13.2f%%\n", name, factor, onePercent, 2.0 * onePercent);
    }

    std::printf("\n(percentages are of a single CPU core)\n");
    return 0;
}
