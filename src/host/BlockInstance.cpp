#include <cstdio>
#include <cstdlib>
#include "host/BlockInstance.h"

namespace blockrig
{

BlockInstance::BlockInstance(std::unique_ptr<juce::AudioPluginInstance> plugin, juce::String blockUid)
    : mPlugin(std::move(plugin))
    , mUid(std::move(blockUid))
{
    if (mPlugin != nullptr)
        mBypassParameter = mPlugin->getBypassParameter();
}

BlockInstance::~BlockInstance()
{
    if (mPlugin != nullptr)
        mPlugin->releaseResources();
}

void BlockInstance::prepare(double sampleRate, int maxBlockSize, bool sourceIsMono)
{
    if (mPlugin == nullptr)
        return;

    mSourceIsMono = sourceIsMono;
    mHasPrepared = true;

    // Negotiate a stereo layout properly rather than just asking for two
    // channels. setPlayConfigDetails alone does not consult the plug-in's bus
    // layouts, so a stereo-capable plug-in could end up mono — processing the
    // left channel and letting the right pass through untouched, which sounds
    // exactly like the stereo image being torn in half.
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto mono = juce::AudioChannelSet::mono();

    const auto tryLayout = [this](const juce::AudioChannelSet& in, const juce::AudioChannelSet& out) {
        auto layout = mPlugin->getBusesLayout();

        if (layout.inputBuses.isEmpty() || layout.outputBuses.isEmpty())
            return false;

        layout.inputBuses.getReference(0) = in;
        layout.outputBuses.getReference(0) = out;

        const bool checked = mPlugin->checkBusesLayoutSupported(layout);
        bool applied = checked && mPlugin->setBusesLayout(layout);

        // Some plug-ins refuse a layout change while prepared even though they
        // support the layout - the check passes, the apply fails. Release, apply,
        // and let the caller's prepareToPlay bring them back.
        if (checked && !applied)
        {
            mPlugin->releaseResources();
            applied = mPlugin->setBusesLayout(layout);
        }

        if (const char* debug = std::getenv("BLOCKRIG_DEBUG_LAYOUT"); debug != nullptr)
            std::fprintf(stderr, "[layout] %s ask %d/%d -> check %d apply %d now %d/%d\n",
                         mPlugin->getName().toRawUTF8(), in.size(), out.size(), static_cast<int>(checked),
                         static_cast<int>(applied), mPlugin->getTotalNumInputChannels(),
                         mPlugin->getTotalNumOutputChannels());

        return applied;
    };

    mMonoOnly = false;

    // A mono source asks for mono in, stereo out first. Stereo/stereo would also
    // succeed here, and that is the trap: the plug-in would then be handed two
    // identical channels and process them symmetrically, so anything that builds
    // its image from the difference between the sides - ping-pong, widening,
    // mid/side - has nothing to work with and stays centred forever.
    const bool widened = sourceIsMono && tryLayout(mono, stereo);

    if (widened)
    {
        mMonoOnly = true;
    }
    else if (!tryLayout(stereo, stereo))
    {
        if (tryLayout(mono, stereo))
        {
            // Mono in, stereo out: feed it the sum, it widens by itself.
            mMonoOnly = true;
        }
        else if (tryLayout(mono, mono))
        {
            mMonoOnly = true;
        }
        // Otherwise keep whatever the plug-in came up with.
    }

    mPlugin->setRateAndBufferSizeDetails(sampleRate, maxBlockSize);
    mPlugin->prepareToPlay(sampleRate, maxBlockSize);

    mNegotiatedIns = mPlugin->getTotalNumInputChannels();
    mNegotiatedOuts = mPlugin->getTotalNumOutputChannels();
    mProducesStereo = mNegotiatedOuts >= 2;

    if (mMonoOnly)
        mMonoScratch.setSize(juce::jmax(1, mPlugin->getTotalNumOutputChannels()), maxBlockSize, false, true, true);

    mLoad.reset();
}

void BlockInstance::release()
{
    if (mPlugin != nullptr)
        mPlugin->releaseResources();
}

juce::String BlockInstance::getDisplayName() const
{
    return mPlugin != nullptr ? mPlugin->getName() : juce::String("(empty)");
}

void BlockInstance::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                            double bufferDurationSeconds) noexcept
{
    if (mPlugin == nullptr)
        return;

    const bool bypassed = isBypassed();

    // Drive the plug-in's own bypass parameter when it has one: plug-ins often
    // crossfade or flush tails properly, which processBlockBypassed cannot do.
    if (mBypassParameter != nullptr && bypassed != mLastAppliedBypass)
    {
        mBypassParameter->setValue(bypassed ? 1.0f : 0.0f);
        mLastAppliedBypass = bypassed;
    }

    const auto start = juce::Time::getHighResolutionTicks();

    if (mMonoOnly && buffer.getNumChannels() >= 2 && mMonoScratch.getNumSamples() >= buffer.getNumSamples())
    {
        // Sum to mono, process once, then send the result to both sides. Better
        // than processing only the left and leaving the right dry.
        const int numSamples = buffer.getNumSamples();
        auto* monoData = mMonoScratch.getWritePointer(0);

        juce::FloatVectorOperations::copy(monoData, buffer.getReadPointer(0), numSamples);
        juce::FloatVectorOperations::add(monoData, buffer.getReadPointer(1), numSamples);
        juce::FloatVectorOperations::multiply(monoData, 0.5f, numSamples);

        for (int channel = 1; channel < mMonoScratch.getNumChannels(); ++channel)
            mMonoScratch.clear(channel, 0, numSamples);

        juce::AudioBuffer<float> view(mMonoScratch.getArrayOfWritePointers(), mMonoScratch.getNumChannels(),
                                      numSamples);

        if (bypassed && mBypassParameter == nullptr)
            mPlugin->processBlockBypassed(view, midi);
        else
            mPlugin->processBlock(view, midi);

        // A mono-out plug-in feeds both sides; a stereo-out one keeps its image.
        const int produced = juce::jmin(mMonoScratch.getNumChannels(), mPlugin->getTotalNumOutputChannels());

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const int source = produced > channel ? channel : 0;
            juce::FloatVectorOperations::copy(buffer.getWritePointer(channel), view.getReadPointer(source),
                                              numSamples);
        }
    }
    else if (bypassed && mBypassParameter == nullptr)
    {
        mPlugin->processBlockBypassed(buffer, midi);
    }
    else
    {
        mPlugin->processBlock(buffer, midi);
    }

    const auto elapsedSeconds =
        juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);

    if (bufferDurationSeconds > 0.0)
        mLoad.addMeasurement(static_cast<float>(elapsedSeconds / bufferDurationSeconds));

    // Peak level leaving this block, with a slow fall so the tile's meter is
    // readable at 15 Hz rather than flickering.
    float peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = juce::jmax(peak, buffer.getMagnitude(channel, 0, buffer.getNumSamples()));

    const auto previous = mOutputLevel.load(std::memory_order_relaxed);
    mOutputLevel.store(peak > previous ? peak : previous * 0.82f, std::memory_order_relaxed);
}

} // namespace blockrig
