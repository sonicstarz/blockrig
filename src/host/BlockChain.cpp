#include "host/BlockChain.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace blockrig
{
namespace
{
/// Constant-power (sin/cos) pan, so a row keeps its perceived level across the
/// image and two hard-panned rows sum back to roughly unity in mono.
void constantPowerPan(float pan, float& leftGain, float& rightGain)
{
    const float normalised = juce::jlimit(0.0f, 1.0f, (pan + 1.0f) * 0.5f);
    const float angle = normalised * juce::MathConstants<float>::halfPi;
    leftGain = std::cos(angle);
    rightGain = std::sin(angle);
}
} // namespace

BlockChain::BlockChain() = default;

BlockChain::~BlockChain()
{
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

    for (auto& rowBuffer : mRowBuffers)
        rowBuffer.setSize(2, mMaxBlockSize, false, true, true);
    mStageInput.setSize(2, mMaxBlockSize, false, true, true);
    mAccumulator.setSize(2, mMaxBlockSize, false, true, true);

    for (auto& stage : mLane)
        for (auto& row : stage.rows)
            for (auto& block : row.blocks)
                block->prepare(mSampleRate, mMaxBlockSize);

    mTotalLoad.reset();
    mDropouts.store(0, std::memory_order_relaxed);

    publishSnapshot();
}

void BlockChain::release()
{
    for (auto& stage : mLane)
        for (auto& row : stage.rows)
            for (auto& block : row.blocks)
                block->release();

    mPrepared = false;
}

BlockChain::LaneRow* BlockChain::rowAt(int stageIndex, int rowIndex)
{
    if (!juce::isPositiveAndBelow(stageIndex, static_cast<int>(mLane.size())))
        return nullptr;

    auto& stage = mLane[static_cast<size_t>(stageIndex)];
    if (!juce::isPositiveAndBelow(rowIndex, static_cast<int>(stage.rows.size())))
        return nullptr;

    return &stage.rows[static_cast<size_t>(rowIndex)];
}

const BlockChain::LaneRow* BlockChain::rowAt(int stageIndex, int rowIndex) const
{
    return const_cast<BlockChain*>(this)->rowAt(stageIndex, rowIndex);
}

int BlockChain::getNumRows(int stageIndex) const
{
    if (!juce::isPositiveAndBelow(stageIndex, static_cast<int>(mLane.size())))
        return 0;
    return static_cast<int>(mLane[static_cast<size_t>(stageIndex)].rows.size());
}

void BlockChain::insertBlock(std::unique_ptr<BlockInstance> block, BlockPosition position)
{
    if (block == nullptr)
        return;

    if (mPrepared)
        block->prepare(mSampleRate, mMaxBlockSize);

    // Past the end of the lane means "make a new stage here".
    const int stageIndex = juce::jlimit(0, static_cast<int>(mLane.size()), position.stage);

    if (stageIndex == static_cast<int>(mLane.size()))
        mLane.emplace_back();

    auto& stage = mLane[static_cast<size_t>(stageIndex)];
    const int rowIndex = juce::jlimit(0, static_cast<int>(stage.rows.size()) - 1, position.row);
    auto& row = stage.rows[static_cast<size_t>(rowIndex)];

    const int index = juce::jlimit(0, static_cast<int>(row.blocks.size()), position.index);
    row.blocks.insert(row.blocks.begin() + index, std::move(block));

    publishSnapshot();
    collectGarbage();
}

void BlockChain::removeBlock(const juce::String& uid)
{
    for (size_t stageIndex = 0; stageIndex < mLane.size(); ++stageIndex)
    {
        auto& stage = mLane[stageIndex];

        for (auto& row : stage.rows)
        {
            const auto found = std::find_if(row.blocks.begin(), row.blocks.end(),
                                            [&uid](const auto& block) { return block->getUid() == uid; });

            if (found == row.blocks.end())
                continue;

            // Keep it alive until no snapshot can still reference it.
            mBlocksAwaitingDeletion.push_back(std::move(*found));
            row.blocks.erase(found);

            // An empty stage is noise in the lane; drop it, unless emptying it
            // was the point of a split the user is still filling in.
            const bool allRowsEmpty = std::all_of(stage.rows.begin(), stage.rows.end(),
                                                  [](const LaneRow& r) { return r.blocks.empty(); });

            if (allRowsEmpty && stage.rows.size() == 1)
                mLane.erase(mLane.begin() + static_cast<long>(stageIndex));

            publishSnapshot();
            collectGarbage();
            return;
        }
    }
}

void BlockChain::moveBlock(const juce::String& uid, BlockPosition position)
{
    std::unique_ptr<BlockInstance> moved;
    int sourceStage = -1;

    for (size_t stageIndex = 0; stageIndex < mLane.size() && moved == nullptr; ++stageIndex)
    {
        for (auto& row : mLane[stageIndex].rows)
        {
            const auto found = std::find_if(row.blocks.begin(), row.blocks.end(),
                                            [&uid](const auto& block) { return block->getUid() == uid; });

            if (found == row.blocks.end())
                continue;

            moved = std::move(*found);
            row.blocks.erase(found);
            sourceStage = static_cast<int>(stageIndex);
            break;
        }
    }

    if (moved == nullptr)
        return;

    int targetStage = juce::jlimit(0, static_cast<int>(mLane.size()), position.stage);

    // Removing the block may have emptied its old stage. Drop it, and shift the
    // target if it sat after the hole.
    if (sourceStage >= 0)
    {
        auto& stage = mLane[static_cast<size_t>(sourceStage)];
        const bool allRowsEmpty = std::all_of(stage.rows.begin(), stage.rows.end(),
                                              [](const LaneRow& r) { return r.blocks.empty(); });

        if (allRowsEmpty && stage.rows.size() == 1)
        {
            mLane.erase(mLane.begin() + sourceStage);
            if (targetStage > sourceStage)
                --targetStage;
        }
    }

    targetStage = juce::jlimit(0, static_cast<int>(mLane.size()), targetStage);

    if (targetStage == static_cast<int>(mLane.size()))
        mLane.emplace_back();

    auto& stage = mLane[static_cast<size_t>(targetStage)];
    const int rowIndex = juce::jlimit(0, static_cast<int>(stage.rows.size()) - 1, position.row);
    auto& row = stage.rows[static_cast<size_t>(rowIndex)];
    const int index = juce::jlimit(0, static_cast<int>(row.blocks.size()), position.index);

    row.blocks.insert(row.blocks.begin() + index, std::move(moved));

    publishSnapshot();
    collectGarbage();
}

bool BlockChain::splitStage(int stageIndex)
{
    if (!juce::isPositiveAndBelow(stageIndex, static_cast<int>(mLane.size())))
        return false;

    auto& stage = mLane[static_cast<size_t>(stageIndex)];

    if (static_cast<int>(stage.rows.size()) >= kMaxRowsPerStage)
        return false;

    // Both paths stay centred, so the split rejoins in true stereo and each path
    // keeps whatever image its plug-ins produce. Hard-panning the sides would
    // collapse every stereo plug-in in the rig to one speaker.
    stage.rows.front().pan = 0.0f;

    LaneRow rowB;
    rowB.pan = 0.0f;
    stage.rows.push_back(std::move(rowB));

    publishSnapshot();
    collectGarbage();
    return true;
}

bool BlockChain::mergeStage(int stageIndex)
{
    if (!juce::isPositiveAndBelow(stageIndex, static_cast<int>(mLane.size())))
        return false;

    auto& stage = mLane[static_cast<size_t>(stageIndex)];

    if (stage.rows.size() <= 1)
        return false;

    // Row A survives; anything below it is retired.
    for (size_t rowIndex = 1; rowIndex < stage.rows.size(); ++rowIndex)
        for (auto& block : stage.rows[rowIndex].blocks)
            mBlocksAwaitingDeletion.push_back(std::move(block));

    stage.rows.resize(1);
    stage.rows.front().pan = 0.0f; // back to centre now that it is alone

    publishSnapshot();
    collectGarbage();
    return true;
}

void BlockChain::setRowGainDb(int stageIndex, int rowIndex, float gainDb)
{
    if (auto* row = rowAt(stageIndex, rowIndex))
    {
        row->gainDb = gainDb;
        publishSnapshot();
        collectGarbage();
    }
}

void BlockChain::setRowPan(int stageIndex, int rowIndex, float pan)
{
    if (auto* row = rowAt(stageIndex, rowIndex))
    {
        row->pan = juce::jlimit(-1.0f, 1.0f, pan);
        publishSnapshot();
        collectGarbage();
    }
}

float BlockChain::getRowGainDb(int stageIndex, int rowIndex) const
{
    const auto* row = rowAt(stageIndex, rowIndex);
    return row != nullptr ? row->gainDb : 0.0f;
}

float BlockChain::getRowPan(int stageIndex, int rowIndex) const
{
    const auto* row = rowAt(stageIndex, rowIndex);
    return row != nullptr ? row->pan : 0.0f;
}

void BlockChain::clear()
{
    for (auto& stage : mLane)
        for (auto& row : stage.rows)
            for (auto& block : row.blocks)
                mBlocksAwaitingDeletion.push_back(std::move(block));

    mLane.clear();
    publishSnapshot();
    collectGarbage();
}

void BlockChain::appendEmptyStage()
{
    mLane.emplace_back();
    publishSnapshot();
    collectGarbage();
}

int BlockChain::getNumBlocks() const
{
    int count = 0;
    for (const auto& stage : mLane)
        for (const auto& row : stage.rows)
            count += static_cast<int>(row.blocks.size());
    return count;
}

BlockInstance* BlockChain::getBlockByUid(const juce::String& uid) const
{
    for (const auto& stage : mLane)
        for (const auto& row : stage.rows)
            for (const auto& block : row.blocks)
                if (block->getUid() == uid)
                    return block.get();
    return nullptr;
}

std::vector<BlockInstance*> BlockChain::getBlocks() const
{
    std::vector<BlockInstance*> result;
    for (const auto& stage : mLane)
        for (const auto& row : stage.rows)
            for (const auto& block : row.blocks)
                result.push_back(block.get());
    return result;
}

BlockInstance* BlockChain::getBlockByIndex(int index) const
{
    const auto blocks = getBlocks();

    if (!juce::isPositiveAndBelow(index, static_cast<int>(blocks.size())))
        return nullptr;

    return blocks[static_cast<size_t>(index)];
}

std::vector<BlockInstance*> BlockChain::getBlocksInRow(int stageIndex, int rowIndex) const
{
    std::vector<BlockInstance*> result;

    if (const auto* row = rowAt(stageIndex, rowIndex))
        for (const auto& block : row->blocks)
            result.push_back(block.get());

    return result;
}

std::optional<BlockPosition> BlockChain::findBlock(const juce::String& uid) const
{
    for (size_t stageIndex = 0; stageIndex < mLane.size(); ++stageIndex)
    {
        const auto& stage = mLane[stageIndex];

        for (size_t rowIndex = 0; rowIndex < stage.rows.size(); ++rowIndex)
        {
            const auto& blocks = stage.rows[rowIndex].blocks;

            for (size_t index = 0; index < blocks.size(); ++index)
                if (blocks[index]->getUid() == uid)
                    return BlockPosition{static_cast<int>(stageIndex), static_cast<int>(rowIndex),
                                         static_cast<int>(index)};
        }
    }

    return std::nullopt;
}

void BlockChain::publishSnapshot()
{
    auto snapshot = std::make_unique<Snapshot>();
    snapshot->stages.reserve(mLane.size());

    int total = 0;

    for (const auto& laneStage : mLane)
    {
        RenderStage stage;
        stage.rows.reserve(laneStage.rows.size());

        for (const auto& laneRow : laneStage.rows)
        {
            RenderRow row;
            row.blocks.reserve(laneRow.blocks.size());

            for (const auto& block : laneRow.blocks)
            {
                row.blocks.push_back(block.get());
                // A bypassed block still contributes latency: JUCE's default
                // bypass does not delay-compensate.
                row.latency += block->getLatencySamples();
            }

            row.gainLinear = juce::Decibels::decibelsToGain(laneRow.gainDb);
            constantPowerPan(laneRow.pan, row.panLeft, row.panRight);

            stage.latency = juce::jmax(stage.latency, row.latency);
            stage.rows.push_back(std::move(row));
        }

        // Align the rows: pad every shorter row up to the longest, which is what
        // stops a split from smearing when the two sides differ in latency.
        for (auto& row : stage.rows)
        {
            row.padSamples = stage.latency - row.latency;

            if (row.padSamples > 0)
            {
                // Allocated here on the message thread; the audio thread only
                // reads and writes it.
                row.padBuffer.setSize(2, row.padSamples + mMaxBlockSize + 1, false, true, true);
                row.padBuffer.clear();
            }
        }

        total += stage.latency;
        snapshot->stages.push_back(std::move(stage));
    }

    snapshot->totalLatencySamples = total;
    mPublishedLatency.store(total, std::memory_order_release);

    Snapshot* previous = mPendingSnapshot.exchange(snapshot.release(), std::memory_order_acq_rel);
    delete previous;
}

bool BlockChain::refreshLatency()
{
    int total = 0;

    for (const auto& stage : mLane)
    {
        int stageLatency = 0;

        for (const auto& row : stage.rows)
        {
            int rowLatency = 0;
            for (const auto& block : row.blocks)
                rowLatency += block->getLatencySamples();
            stageLatency = juce::jmax(stageLatency, rowLatency);
        }

        total += stageLatency;
    }

    if (total == mPublishedLatency.load(std::memory_order_acquire))
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
        return; // Queue full: reclaimed at teardown instead.

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

    // A removed block is only safe to destroy once the audio thread has adopted a
    // snapshot that no longer mentions it. Adoption clears mPendingSnapshot, so
    // an empty pending slot means the newest lane is live. (If audio is stopped,
    // pending stays non-null and deletion is deferred until teardown, which is
    // also safe.)
    if (!mBlocksAwaitingDeletion.empty() && mPendingSnapshot.load(std::memory_order_acquire) == nullptr)
        mBlocksAwaitingDeletion.clear(); // ~BlockInstance releases the plug-in
}

void BlockChain::processStage(RenderStage& stage, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                              double bufferDuration) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin(2, buffer.getNumChannels());

    // The common case: one row, processed straight through.
    if (stage.rows.size() == 1)
    {
        auto& row = stage.rows.front();

        for (auto* block : row.blocks)
            block->process(buffer, midi, bufferDuration);

        return;
    }

    // A split. Keep the stage's input so every row starts from the same signal.
    for (int channel = 0; channel < numChannels; ++channel)
        mStageInput.copyFrom(channel, 0, buffer, channel, 0, numSamples);

    mAccumulator.clear(0, numSamples);

    for (size_t rowIndex = 0; rowIndex < stage.rows.size() && rowIndex < mRowBuffers.size(); ++rowIndex)
    {
        auto& row = stage.rows[rowIndex];
        auto& work = mRowBuffers[rowIndex];

        for (int channel = 0; channel < numChannels; ++channel)
            work.copyFrom(channel, 0, mStageInput, channel, 0, numSamples);

        for (auto* block : row.blocks)
            block->process(work, midi, bufferDuration);

        // Delay the shorter rows so the split stays phase-aligned.
        if (row.padSamples > 0 && row.padBuffer.getNumSamples() > 0)
        {
            const int capacity = row.padBuffer.getNumSamples();

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* data = work.getWritePointer(channel);
                auto* delayLine = row.padBuffer.getWritePointer(channel);
                int writePosition = row.padWritePosition;

                for (int i = 0; i < numSamples; ++i)
                {
                    const int readPosition = (writePosition + capacity - row.padSamples) % capacity;
                    const float delayed = delayLine[readPosition];
                    delayLine[writePosition] = data[i];
                    data[i] = delayed;
                    writePosition = (writePosition + 1) % capacity;
                }
            }

            row.padWritePosition = (row.padWritePosition + numSamples) % capacity;
        }

        // Row gain and pan into the sum.
        const float leftGain = row.gainLinear * row.panLeft;
        const float rightGain = row.gainLinear * row.panRight;

        // Centre means "leave the stereo image alone": each side of the row goes
        // to its own side of the sum. Only an actual pan setting steers it.
        if (std::abs(row.panLeft - row.panRight) < 1.0e-4f)
        {
            const float gain = row.gainLinear * row.panLeft;

            for (int channel = 0; channel < numChannels; ++channel)
                mAccumulator.addFrom(channel, 0, work, juce::jmin(channel, work.getNumChannels() - 1), 0,
                                     numSamples, gain);
        }
        else
        {
            // Panned: collapse the row and steer it, which is what a pan means.
            if (numChannels >= 1)
                mAccumulator.addFrom(0, 0, work, 0, 0, numSamples, leftGain);
            if (numChannels >= 2)
                mAccumulator.addFrom(1, 0, work, juce::jmin(1, work.getNumChannels() - 1), 0, numSamples,
                                     rightGain);
        }
    }

    for (int channel = 0; channel < numChannels; ++channel)
        buffer.copyFrom(channel, 0, mAccumulator, channel, 0, numSamples);
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

    // Hosts occasionally exceed the block size they promised, and our scratch
    // buffers are sized for that promise.
    if (numSamples > mStageInput.getNumSamples())
        return;

    const double bufferDuration = static_cast<double>(numSamples) / mSampleRate;
    const auto start = juce::Time::getHighResolutionTicks();

    for (auto& stage : mActiveSnapshot->stages)
        processStage(stage, buffer, midi, bufferDuration);

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
