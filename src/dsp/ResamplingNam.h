#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <NAM/dsp.h>
#include <NAM/slimmable.h>

#include "dsp/ResamplerShim.h"

namespace nammodeler
{

/// Wraps a nam::DSP so it can run at any host sample rate.
///
/// When the host runs at the model's own rate (the common 48 kHz case) the
/// resampler is bypassed entirely and latency is zero. Otherwise the model is
/// sandwiched between two Lanczos resamplers and reports their latency.
///
/// Owns the float <-> NAM_SAMPLE conversion so the rest of the plugin can stay
/// in JUCE's float world. All buffers are preallocated in reset().
class ResamplingNam
{
public:
    /// Models predating sample-rate metadata are assumed to be 48 kHz, which is
    /// what they almost always are (same assumption the official plugin makes).
    static constexpr double kAssumedSampleRate = 48000.0;

    explicit ResamplingNam(std::unique_ptr<nam::DSP> model);

    /// Not real-time safe: allocates, and prewarms the model. Call from a
    /// background thread or while audio is stopped.
    void reset(double hostSampleRate, int maxBlockSize);

    /// Processes mono audio in place. Blocks larger than the size passed to
    /// reset() are split, so an over-eager host cannot overrun our buffers.
    void process(float* samples, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return needToResample() ? mResampler.GetLatency() : 0; }

    double getModelSampleRate() const noexcept { return mModelSampleRate; }
    bool needToResample() const noexcept { return mHostSampleRate != mModelSampleRate; }

    bool hasLoudness() const { return mModel->HasLoudness(); }
    double getLoudness() const { return mModel->GetLoudness(); }
    bool hasInputLevel() const { return mModel->HasInputLevel(); }
    double getInputLevel() const { return mModel->GetInputLevel(); }
    bool hasOutputLevel() const { return mModel->HasOutputLevel(); }
    double getOutputLevel() const { return mModel->GetOutputLevel(); }

    /// Non-null only for A2 slimmable/container models.
    nam::SlimmableModel* getSlimmableModel() { return dynamic_cast<nam::SlimmableModel*>(mModel.get()); }

private:
    void processChunk(float* samples, int numSamples) noexcept;

    std::unique_ptr<nam::DSP> mModel;
    dsp::ResamplingContainer<NAM_SAMPLE, 1, 12> mResampler;
    std::function<void(NAM_SAMPLE**, NAM_SAMPLE**, int)> mProcessFunc;

    double mModelSampleRate = kAssumedSampleRate;
    double mHostSampleRate = 0.0;
    int mMaxBlockSize = 0;

    std::vector<NAM_SAMPLE> mInputScratch;
    std::vector<NAM_SAMPLE> mOutputScratch;
};

} // namespace nammodeler
