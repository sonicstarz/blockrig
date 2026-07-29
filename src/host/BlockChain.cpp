#include "host/BlockChain.h"

#include <algorithm>
#include <utility>

namespace blockrig
{

BlockChain::BlockChain() = default;

BlockChain::~BlockChain()
{
    // Drop the lane, then reclaim everything the audio thread handed back.
    delete mPendingSnapshot.exchange(nullptr, std::memory_order_acq_rel);
    collectGarbage();
    delete mActiveSnapshot;
    mActiveSnapshot = nullptr;
}

void BlockChain::prepare(double sampleRate, int maxBlockSize)
{
    mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    mMaxBlockSize = juce::jmax(1, maxBlockSize);
    mPrepared = true;

    mScratch.setSize(2, mMaxBlockSize, false, true, true);

    // prepareToPlay runs with audio stopped, so preparing blocks in place is safe.
    for (auto& block : mBlocks)
        block->prepare(mSampleRate, mMaxBlockSize);

    mTotalLoad.reset();
    mDropouts.store(0, std::memory_order_relaxed);

    publishSnapshot();
}

void BlockChain::release()
{
    for (auto& block : mBlocks)
        block->release();

    mPrepared = false;
}

void BlockChain::insertBlock(std::unique_ptr<BlockInstance> block, int index)
{
    if (block == nullptr)
        return;

    if (mPrepared)
        block->prepare(mSampleRate, mMaxBlockSize);

    const int clamped = juce::jlimit(0, static_cast<int>(mBlocks.size()), index);
    mBlocks.insert(mBlocks.begin() + clamped, std::move(block));

    publishSnapshot();
    collectGarbage();
}

void BlockChain::removeBlock(const juce::String& uid)
{
    const auto found = std::find_if(mBlocks.begin(), mBlocks.end(),
                                    [&uid](const auto& block) { return block->getUid() == uid; });

    if (found == mBlocks.end())
        return;

    // Keep it alive until no snapshot can still reference it.
    mBlocksAwaitingDeletion.push_back(std::move(*found));
    mBlocks.erase(found);

    publishSnapshot();
    collectGarbage();
}

void BlockChain::moveBlock(const juce::String& uid, int newIndex)
{
    const auto found = std::find_if(mBlocks.begin(), mBlocks.end(),
                                    [&uid](const auto& block) { return block->getUid() == uid; });

    if (found == mBlocks.end())
        return;

    auto moved = std::move(*found);
    mBlocks.erase(found);

    const int clamped = juce::jlimit(0, static_cast<int>(mBlocks.size()), newIndex);
    mBlocks.insert(mBlocks.begin() + clamped, std::move(moved));

    publishSnapshot();
    collectGarbage();
}

void BlockChain::clear()
{
    for (auto& block : mBlocks)
        mBlocksAwaitingDeletion.push_back(std::move(block));

    mBlocks.clear();
    publishSnapshot();
    collectGarbage();
}

int BlockChain::getNumBlocks() const
{
    return static_cast<int>(mBlocks.size());
}

BlockInstance* BlockChain::getBlockByUid(const juce::String& uid) const
{
    const auto found = std::find_if(mBlocks.begin(), mBlocks.end(),
                                    [&uid](const auto& block) { return block->getUid() == uid; });
    return found != mBlocks.end() ? found->get() : nullptr;
}

BlockInstance* BlockChain::getBlockByIndex(int index) const
{
    if (!juce::isPositiveAndBelow(index, static_cast<int>(mBlocks.size())))
        return nullptr;
    return mBlocks[static_cast<size_t>(index)].get();
}

std::vector<BlockInstance*> BlockChain::getBlocks() const
{
    std::vector<BlockInstance*> result;
    result.reserve(mBlocks.size());
    for (const auto& block : mBlocks)
        result.push_back(block.get());
    return result;
}

void BlockChain::publishSnapshot()
{
    auto snapshot = std::make_unique<Snapshot>();
    snapshot->blocks.reserve(mBlocks.size());

    int latency = 0;
    for (const auto& block : mBlocks)
    {
        snapshot->blocks.push_back(block.get());
        // A bypassed block still contributes latency: JUCE's default bypass does
        // not delay-compensate, so the chain must keep reporting it.
        latency += block->getLatencySamples();
    }

    snapshot->totalLatencySamples = latency;
    mPublishedLatency.store(latency, std::memory_order_release);

    // Anything the audio thread has not picked up yet is ours to destroy.
    Snapshot* previous = mPendingSnapshot.exchange(snapshot.release(), std::memory_order_acq_rel);
    delete previous;
}

bool BlockChain::refreshLatency()
{
    int latency = 0;
    for (const auto& block : mBlocks)
        latency += block->getLatencySamples();

    if (latency == mPublishedLatency.load(std::memory_order_acquire))
        return false;

    publishSnapshot();
    collectGarbage();
    return true;
}

void BlockChain::retireSnapshot(Snapshot* snapshot) noexcept
{
    if (snapshot == nullptr)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    mRetireFifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 + size2 < 1)
        return; // Queue full: the message thread will reclaim it at teardown.

    mRetireSlots[static_cast<size_t>(size1 > 0 ? start1 : start2)] = snapshot;
    mRetireFifo.finishedWrite(1);
}

void BlockChain::collectGarbage()
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    mRetireFifo.prepareToRead(mRetireFifo.getNumReady(), start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
        delete std::exchange(mRetireSlots[static_cast<size_t>(start1 + i)], nullptr);
    for (int i = 0; i < size2; ++i)
        delete std::exchange(mRetireSlots[static_cast<size_t>(start2 + i)], nullptr);

    mRetireFifo.finishedRead(size1 + size2);

    // A removed block is only safe to destroy once the audio thread has adopted
    // a snapshot that no longer mentions it. Adoption clears mPendingSnapshot,
    // so an empty pending slot means the newest lane is live.
    // (If audio is stopped, pending stays non-null and deletion is simply
    // deferred until teardown, which is safe.)
    if (!mBlocksAwaitingDeletion.empty() && mPendingSnapshot.load(std::memory_order_acquire) == nullptr)
        mBlocksAwaitingDeletion.clear(); // ~BlockInstance releases the plug-in
}

void BlockChain::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept
{
    // Adopt a newly published lane, handing the old one back for deletion.
    if (Snapshot* pending = mPendingSnapshot.exchange(nullptr, std::memory_order_acq_rel))
    {
        retireSnapshot(mActiveSnapshot);
        mActiveSnapshot = pending;
    }

    if (mActiveSnapshot == nullptr)
        return;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const double bufferDuration = static_cast<double>(numSamples) / mSampleRate;
    const auto start = juce::Time::getHighResolutionTicks();

    for (auto* block : mActiveSnapshot->blocks)
        block->process(buffer, midi, bufferDuration);

    const auto elapsed = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);

    if (bufferDuration > 0.0)
    {
        const auto fraction = static_cast<float>(elapsed / bufferDuration);
        mTotalLoad.addMeasurement(fraction);

        if (fraction >= 1.0f)
            mDropouts.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace blockrig
