#include "dsp/ModelLoader.h"

#include <cmath>
#include <memory>
#include <stdexcept>

#include <NAM/get_dsp.h>
#include <json.hpp>

namespace nammodeler
{
namespace
{
constexpr int kIdleWaitMs = 200;

juce::String describeModel(const nlohmann::json& config, const juce::String& fallbackName)
{
    if (config.contains("metadata") && config["metadata"].is_object())
    {
        const auto& metadata = config["metadata"];
        if (metadata.contains("name") && metadata["name"].is_string())
        {
            const auto name = juce::String(metadata["name"].get<std::string>()).trim();
            if (name.isNotEmpty())
                return name;
        }
    }
    return fallbackName;
}
} // namespace

ModelLoader::ModelLoader()
    : juce::Thread("NAM Model Loader")
{
    mSlots.fill(nullptr);
    for (auto& target : mSlimTargets)
        target.store(1.0);
    mAppliedSlim.fill(-1.0);

    startThread(juce::Thread::Priority::normal);
}

ModelLoader::~ModelLoader()
{
    mAlive->store(false);
    stopThread(4000);

    // Final drain so nothing outlives us.
    for (auto* slot : mSlots)
        if (slot != nullptr)
            slot->drainRetired();
}

void ModelLoader::attachSlot(int slotIndex, AmpSlot* slot)
{
    if (juce::isPositiveAndBelow(slotIndex, kNumSlots))
        mSlots[static_cast<size_t>(slotIndex)] = slot;
}

void ModelLoader::setAudioConfiguration(double sampleRate, int maxBlockSize)
{
    mSampleRate.store(sampleRate > 0.0 ? sampleRate : 48000.0);
    mMaxBlockSize.store(juce::jmax(1, maxBlockSize));
}

void ModelLoader::loadFromFile(int slotIndex, const juce::File& file)
{
    if (!file.existsAsFile())
    {
        if (onLoadFinished)
        {
            const auto name = file.getFileName();
            juce::MessageManager::callAsync([this, alive = mAlive, slotIndex, name] {
                if (alive->load() && onLoadFinished)
                    onLoadFinished(slotIndex, ModelInfo{}, "File not found: " + name);
            });
        }
        return;
    }

    loadFromJson(slotIndex, file.loadFileAsString(), file.getFileNameWithoutExtension(), file.getFullPathName());
}

void ModelLoader::loadFromJson(int slotIndex, juce::String json, juce::String name, juce::String path)
{
    if (!juce::isPositiveAndBelow(slotIndex, kNumSlots))
        return;

    {
        const juce::ScopedLock lock(mRequestLock);
        // A newer request supersedes anything not yet picked up.
        mPendingRequests[static_cast<size_t>(slotIndex)] =
            Request{std::move(json), std::move(name), std::move(path), false};
    }

    notify();
}

void ModelLoader::clearSlot(int slotIndex)
{
    if (!juce::isPositiveAndBelow(slotIndex, kNumSlots))
        return;

    {
        const juce::ScopedLock lock(mRequestLock);
        mPendingRequests[static_cast<size_t>(slotIndex)] = Request{{}, {}, {}, true};
    }

    notify();
}

void ModelLoader::setSlimSize(int slotIndex, double value)
{
    if (!juce::isPositiveAndBelow(slotIndex, kNumSlots))
        return;

    mSlimTargets[static_cast<size_t>(slotIndex)].store(juce::jlimit(0.0, 1.0, value));
    notify();
}

void ModelLoader::run()
{
    while (!threadShouldExit())
    {
        for (int slotIndex = 0; slotIndex < kNumSlots && !threadShouldExit(); ++slotIndex)
        {
            std::optional<Request> request;
            {
                const juce::ScopedLock lock(mRequestLock);
                request.swap(mPendingRequests[static_cast<size_t>(slotIndex)]);
            }

            if (request.has_value())
                processRequest(slotIndex, std::move(*request));
        }

        // Slim sizes are applied to the live model, which is why this must run
        // before draining: a retired model stays alive until we drain it.
        applyPendingSlimSizes();

        for (auto* slot : mSlots)
            if (slot != nullptr)
                slot->drainRetired();

        wait(kIdleWaitMs);
    }
}

void ModelLoader::applyPendingSlimSizes()
{
    for (int slotIndex = 0; slotIndex < kNumSlots; ++slotIndex)
    {
        auto* slot = mSlots[static_cast<size_t>(slotIndex)];
        if (slot == nullptr)
            continue;

        const double target = mSlimTargets[static_cast<size_t>(slotIndex)].load();
        if (std::abs(target - mAppliedSlim[static_cast<size_t>(slotIndex)]) < 1.0e-9)
            continue;

        auto* model = slot->getActiveModelForBackgroundThread();
        if (model == nullptr)
            continue;

        if (auto* slimmable = model->getSlimmableModel())
        {
            slimmable->SetSlimmableSize(target);
            mAppliedSlim[static_cast<size_t>(slotIndex)] = target;
        }
    }
}

void ModelLoader::processRequest(int slotIndex, Request request)
{
    auto* slot = mSlots[static_cast<size_t>(slotIndex)];
    if (slot == nullptr)
        return;

    if (request.clear)
    {
        slot->requestClear();
        mAppliedSlim[static_cast<size_t>(slotIndex)] = -1.0;
        if (onLoadFinished)
            juce::MessageManager::callAsync([this, alive = mAlive, slotIndex] {
                if (alive->load() && onLoadFinished)
                    onLoadFinished(slotIndex, ModelInfo{}, {});
            });
        return;
    }

    ModelInfo info;
    juce::String error;

    try
    {
        const auto config = nlohmann::json::parse(request.json.toStdString());

        auto model = nam::get_dsp(config);
        if (model == nullptr)
            throw std::runtime_error("Model could not be created from this file.");

        if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
            throw std::runtime_error("Only mono (1-in / 1-out) models are supported, but this one is "
                                     + std::to_string(model->NumInputChannels()) + "-in / "
                                     + std::to_string(model->NumOutputChannels()) + "-out.");

        auto wrapped = std::make_unique<ResamplingNam>(std::move(model));

        // Allocates and prewarms: the expensive part, and the reason this class exists.
        wrapped->reset(mSampleRate.load(), mMaxBlockSize.load());

        // Carry the current slim setting into the new model before it goes live.
        if (auto* slimmable = wrapped->getSlimmableModel())
        {
            const double target = mSlimTargets[static_cast<size_t>(slotIndex)].load();
            slimmable->SetSlimmableSize(target);
            mAppliedSlim[static_cast<size_t>(slotIndex)] = target;
        }

        info.name = describeModel(config, request.name);
        info.path = request.path;
        info.json = request.json;
        info.metrics.hasLoudness = wrapped->hasLoudness();
        if (info.metrics.hasLoudness)
            info.metrics.loudness = wrapped->getLoudness();
        info.metrics.hasInputLevel = wrapped->hasInputLevel();
        if (info.metrics.hasInputLevel)
            info.metrics.inputLevel = wrapped->getInputLevel();
        info.metrics.hasOutputLevel = wrapped->hasOutputLevel();
        if (info.metrics.hasOutputLevel)
            info.metrics.outputLevel = wrapped->getOutputLevel();
        info.metrics.modelSampleRate = wrapped->getModelSampleRate();
        info.metrics.resampling = wrapped->needToResample();
        info.metrics.latencySamples = wrapped->getLatencySamples();
        info.metrics.slimmable = wrapped->getSlimmableModel() != nullptr;

        slot->stageModel(std::move(wrapped));
    }
    catch (const std::exception& e)
    {
        error = juce::String(e.what());
        info = ModelInfo{};
    }
    catch (...)
    {
        error = "Unknown error while loading model.";
        info = ModelInfo{};
    }

    if (onLoadFinished)
    {
        juce::MessageManager::callAsync([this, alive = mAlive, slotIndex, info, error] {
            if (alive->load() && onLoadFinished)
                onLoadFinished(slotIndex, info, error);
        });
    }
}

} // namespace nammodeler
