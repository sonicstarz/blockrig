#include "dsp/AmpSlot.h"

#include <algorithm>
#include <utility>

namespace nammodeler
{
namespace
{
constexpr float kSmoothingSeconds = 0.02f;
constexpr double kNormalizedTargetLoudness = -18.0;

ModelMetrics gatherMetrics(ResamplingNam& model)
{
    ModelMetrics metrics;
    metrics.hasLoudness = model.hasLoudness();
    if (metrics.hasLoudness)
        metrics.loudness = model.getLoudness();
    metrics.hasInputLevel = model.hasInputLevel();
    if (metrics.hasInputLevel)
        metrics.inputLevel = model.getInputLevel();
    metrics.hasOutputLevel = model.hasOutputLevel();
    if (metrics.hasOutputLevel)
        metrics.outputLevel = model.getOutputLevel();
    metrics.modelSampleRate = model.getModelSampleRate();
    metrics.resampling = model.needToResample();
    metrics.latencySamples = model.getLatencySamples();
    metrics.slimmable = model.getSlimmableModel() != nullptr;
    return metrics;
}
} // namespace

AmpSlot::AmpSlot() = default;

AmpSlot::~AmpSlot()
{
    delete mStaged.exchange(nullptr, std::memory_order_acq_rel);
    drainRetired();
}

void AmpSlot::prepare(double sampleRate, int maxBlockSize)
{
    mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    mMaxBlockSize = std::max(1, maxBlockSize);

    mToneStack.prepare(mSampleRate);
    mDCBlocker.prepare(mSampleRate);

    mInputGain.reset(mSampleRate, static_cast<double>(kSmoothingSeconds));
    mOutputGain.reset(mSampleRate, static_cast<double>(kSmoothingSeconds));

    // prepareToPlay runs with audio stopped, so resetting the model here is safe.
    if (mActive != nullptr)
    {
        mActive->reset(mSampleRate, mMaxBlockSize);
        mMetrics = gatherMetrics(*mActive);
        mLatencySamples.store(mMetrics.latencySamples, std::memory_order_release);
    }
}

void AmpSlot::reset()
{
    mToneStack.reset();
    mDCBlocker.reset();
}

void AmpSlot::stageModel(std::unique_ptr<ResamplingNam> model)
{
    mClearRequested.store(false, std::memory_order_release);

    // Anything the audio thread has not picked up yet is ours to destroy.
    ResamplingNam* previous = mStaged.exchange(model.release(), std::memory_order_acq_rel);
    delete previous;
}

void AmpSlot::drainRetired()
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    mRetireFifo.prepareToRead(mRetireFifo.getNumReady(), start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
        delete std::exchange(mRetireSlots[static_cast<size_t>(start1 + i)], nullptr);
    for (int i = 0; i < size2; ++i)
        delete std::exchange(mRetireSlots[static_cast<size_t>(start2 + i)], nullptr);

    mRetireFifo.finishedRead(size1 + size2);
}

bool AmpSlot::retire(ResamplingNam* model) noexcept
{
    if (model == nullptr)
        return true;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    mRetireFifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 + size2 < 1)
        return false; // Loader thread has not drained yet; try again next block.

    mRetireSlots[static_cast<size_t>(size1 > 0 ? start1 : start2)] = model;
    mRetireFifo.finishedWrite(1);
    return true;
}

void AmpSlot::adoptStagedModel() noexcept
{
    if (mClearRequested.load(std::memory_order_acquire))
    {
        if (mActive == nullptr)
        {
            mClearRequested.store(false, std::memory_order_release);
        }
        else if (retire(mActive.get()))
        {
            (void)mActive.release();
            mActiveRaw.store(nullptr, std::memory_order_release);
            mMetrics = ModelMetrics{};
            mHasModel.store(false, std::memory_order_release);
            mLatencySamples.store(0, std::memory_order_release);
            mClearRequested.store(false, std::memory_order_release);
        }
    }

    ResamplingNam* staged = mStaged.load(std::memory_order_acquire);
    if (staged == nullptr)
        return;

    // Hand the outgoing model off for destruction *before* claiming the new
    // one, so there is no path where this thread ends up having to free it.
    if (mActive != nullptr)
    {
        if (!retire(mActive.get()))
            return; // Loader has not drained yet; try again next block.

        (void)mActive.release();
        mActiveRaw.store(nullptr, std::memory_order_release);
        mMetrics = ModelMetrics{};
        mHasModel.store(false, std::memory_order_release);
        mLatencySamples.store(0, std::memory_order_release);
    }

    if (!mStaged.compare_exchange_strong(staged, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed))
        return; // The loader staged a newer model; we adopt that one next block.

    mActive.reset(staged);
    mActiveRaw.store(staged, std::memory_order_release);
    mMetrics = gatherMetrics(*mActive);
    mToneStack.reset();
    mDCBlocker.reset();
    mHasModel.store(true, std::memory_order_release);
    mLatencySamples.store(mMetrics.latencySamples, std::memory_order_release);
}

bool AmpSlot::process(float* samples, int numSamples, const SlotParameters& params) noexcept
{
    adoptStagedModel();

    if (mActive == nullptr || !params.enabled)
        return false;

    // Input trim, plus optional calibration aligning the interface's level to
    // the level the model was captured at.
    float inputGainDb = params.inputTrimDb;
    if (params.calibrateInput && mMetrics.hasInputLevel)
        inputGainDb += params.calibrationDbu - static_cast<float>(mMetrics.inputLevel);

    // Output trim, plus the model-derived output mode.
    float outputGainDb = params.outputTrimDb;
    switch (params.outputMode)
    {
        case OutputMode::normalized:
            if (mMetrics.hasLoudness)
                outputGainDb += static_cast<float>(kNormalizedTargetLoudness - mMetrics.loudness);
            break;
        case OutputMode::calibrated:
            if (mMetrics.hasOutputLevel)
                outputGainDb += static_cast<float>(mMetrics.outputLevel) - params.calibrationDbu;
            break;
        case OutputMode::raw:
        default:
            break;
    }

    mInputGain.setTargetValue(juce::Decibels::decibelsToGain(inputGainDb));
    mOutputGain.setTargetValue(juce::Decibels::decibelsToGain(outputGainDb));

    for (int i = 0; i < numSamples; ++i)
        samples[i] *= mInputGain.getNextValue();

    mActive->process(samples, numSamples);

    for (int i = 0; i < numSamples; ++i)
        samples[i] = mDCBlocker.processSample(samples[i]);

    if (params.eqEnabled)
    {
        mToneStack.setKnobs(params.bass, params.mid, params.treble);
        mToneStack.processBlock(samples, numSamples);
    }

    const float polarity = params.phaseInvert ? -1.0f : 1.0f;
    for (int i = 0; i < numSamples; ++i)
        samples[i] *= mOutputGain.getNextValue() * polarity;

    return true;
}

} // namespace nammodeler
