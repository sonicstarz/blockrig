#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "dsp/ResamplingNam.h"
#include "dsp/ToneStack.h"

namespace nammodeler
{

/// How a capture's output level is derived. Order matches the block's parameter
/// and must not change: hosts store automation by index.
enum class OutputMode
{
    raw = 0,
    normalized = 1,
    calibrated = 2
};


/// Everything the audio thread needs to know about the loaded model, cached at
/// adoption time so the per-block path never touches the model's accessors.
struct ModelMetrics
{
    bool hasLoudness = false;
    double loudness = 0.0;
    bool hasInputLevel = false;
    double inputLevel = 0.0;
    bool hasOutputLevel = false;
    double outputLevel = 0.0;
    double modelSampleRate = 48000.0;
    bool resampling = false;
    int latencySamples = 0;
    bool slimmable = false;
};

/// Message-thread facing description of a slot's model, used by the editor and
/// by state save/restore.
struct ModelInfo
{
    juce::String name;
    juce::String path;
    juce::String json; // full .nam contents, embedded in plugin state
    ModelMetrics metrics;
};

/// One-pole DC blocker. The official plugin runs one at 5 Hz after the model,
/// because neural models can emit a small DC offset.
class DCBlocker
{
public:
    static constexpr double kFrequency = 5.0;

    void prepare(double sampleRate)
    {
        mR = 1.0 - (2.0 * 3.14159265358979323846 * kFrequency / (sampleRate > 0.0 ? sampleRate : 48000.0));
        reset();
    }

    void reset() { mX1 = mY1 = 0.0; }

    inline float processSample(float input) noexcept
    {
        const double x = static_cast<double>(input);
        double y = x - mX1 + mR * mY1;
        if (!std::isfinite(y))
        {
            y = 0.0;
            mX1 = 0.0;
        }
        mX1 = x;
        mY1 = y;
        return static_cast<float>(y);
    }

private:
    double mR = 0.999;
    double mX1 = 0.0;
    double mY1 = 0.0;
};

/// Per-block snapshot of the slot's parameters, read from the APVTS by the
/// processor so the audio thread never does string lookups.
struct SlotParameters
{
    bool enabled = true;
    float inputTrimDb = 0.0f;
    float outputTrimDb = 0.0f;
    bool phaseInvert = false;
    bool eqEnabled = true;
    float bass = 5.0f;
    float mid = 5.0f;
    float treble = 5.0f;
    OutputMode outputMode = OutputMode::normalized;
    bool calibrateInput = false;
    float calibrationDbu = 12.0f;
};

/// One amp channel: input trim -> NAM model -> DC blocker -> output-mode gain
/// -> tone stack -> output trim -> phase.
///
/// Models are built on a background thread and handed over through an atomic
/// staging pointer; the audio thread adopts them and pushes the outgoing model
/// onto a retirement queue so destruction never happens under the audio lock.
class AmpSlot
{
public:
    AmpSlot();
    ~AmpSlot();

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    /// Audio thread. Processes mono in place. Returns false if the slot
    /// produced nothing (no model, or disabled), in which case `samples` is
    /// left untouched and should be treated as silence by the caller.
    bool process(float* samples, int numSamples, const SlotParameters& params) noexcept;

    /// Background thread. Takes ownership; replaces any not-yet-adopted model.
    void stageModel(std::unique_ptr<ResamplingNam> model);

    /// Background thread. Asks the audio thread to drop the active model.
    void requestClear() { mClearRequested.store(true, std::memory_order_release); }

    /// Background thread. Destroys models the audio thread has finished with.
    void drainRetired();

    /// Background thread only, and only valid until the next drainRetired()
    /// call on that same thread: a retired model stays alive until the loader
    /// drains it, so the loader may safely touch this pointer before draining.
    /// Used to apply A2 slim-size changes, which are thread-safe but not
    /// real-time safe.
    ResamplingNam* getActiveModelForBackgroundThread() const noexcept
    {
        return mActiveRaw.load(std::memory_order_acquire);
    }

    bool hasModel() const noexcept { return mHasModel.load(std::memory_order_acquire); }
    int getLatencySamples() const noexcept { return mLatencySamples.load(std::memory_order_acquire); }

private:
    void adoptStagedModel() noexcept;
    bool retire(ResamplingNam* model) noexcept;

    // Audio-thread state.
    std::unique_ptr<ResamplingNam> mActive;
    ModelMetrics mMetrics;
    ToneStack mToneStack;
    juce::SmoothedValue<float> mInputGain;
    juce::SmoothedValue<float> mOutputGain;
    DCBlocker mDCBlocker;

    double mSampleRate = 48000.0;
    int mMaxBlockSize = 512;

    // Cross-thread handover.
    std::atomic<ResamplingNam*> mStaged{nullptr};
    std::atomic<ResamplingNam*> mActiveRaw{nullptr};
    std::atomic<bool> mClearRequested{false};
    std::atomic<bool> mHasModel{false};
    std::atomic<int> mLatencySamples{0};

    // Single-producer (audio) / single-consumer (loader) retirement queue.
    static constexpr int kRetireCapacity = 32;
    juce::AbstractFifo mRetireFifo{kRetireCapacity};
    std::array<ResamplingNam*, kRetireCapacity> mRetireSlots{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpSlot)
};

} // namespace nammodeler
