#include "dsp/ResamplingNam.h"

#include <algorithm>
#include <cmath>

namespace nammodeler
{
namespace
{
double resolveModelSampleRate(const nam::DSP& model)
{
    const double reported = model.GetExpectedSampleRate();
    return reported <= 0.0 ? ResamplingNam::kAssumedSampleRate : reported;
}
} // namespace

ResamplingNam::ResamplingNam(std::unique_ptr<nam::DSP> model)
    : mModel(std::move(model))
    , mResampler(resolveModelSampleRate(*mModel))
    , mModelSampleRate(resolveModelSampleRate(*mModel))
{
    // Capturing only `this` keeps the std::function inside its small-buffer
    // optimisation, so calling it on the audio thread cannot allocate.
    mProcessFunc = [this](NAM_SAMPLE** input, NAM_SAMPLE** output, int numFrames) {
        mModel->process(input, output, numFrames);
    };
}

void ResamplingNam::reset(double hostSampleRate, int maxBlockSize)
{
    mHostSampleRate = hostSampleRate > 0.0 ? hostSampleRate : mModelSampleRate;
    mMaxBlockSize = std::max(1, maxBlockSize);

    mInputScratch.assign(static_cast<size_t>(mMaxBlockSize), NAM_SAMPLE(0));
    mOutputScratch.assign(static_cast<size_t>(mMaxBlockSize), NAM_SAMPLE(0));

    if (needToResample())
    {
        mResampler.Reset(mHostSampleRate, mMaxBlockSize);

        // Upsampling means the model sees more samples than the host block has.
        const double upRatio = mHostSampleRate / mModelSampleRate;
        const auto maxEncapsulated = static_cast<int>(std::ceil(static_cast<double>(mMaxBlockSize) / upRatio));
        mModel->ResetAndPrewarm(mModelSampleRate, maxEncapsulated);
    }
    else
    {
        mModel->ResetAndPrewarm(mModelSampleRate, mMaxBlockSize);
    }
}

void ResamplingNam::process(float* samples, int numSamples) noexcept
{
    // Hosts occasionally exceed the block size they promised in prepareToPlay.
    while (numSamples > 0)
    {
        const int chunk = std::min(numSamples, mMaxBlockSize);
        processChunk(samples, chunk);
        samples += chunk;
        numSamples -= chunk;
    }
}

void ResamplingNam::processChunk(float* samples, int numSamples) noexcept
{
    NAM_SAMPLE* inputPointers[1] = {mInputScratch.data()};
    NAM_SAMPLE* outputPointers[1] = {mOutputScratch.data()};

    for (int i = 0; i < numSamples; ++i)
        mInputScratch[static_cast<size_t>(i)] = static_cast<NAM_SAMPLE>(samples[i]);

    if (needToResample())
        mResampler.ProcessBlock(inputPointers, outputPointers, numSamples, mProcessFunc);
    else
        mModel->process(inputPointers, outputPointers, numSamples);

    for (int i = 0; i < numSamples; ++i)
        samples[i] = static_cast<float>(mOutputScratch[static_cast<size_t>(i)]);
}

} // namespace nammodeler
