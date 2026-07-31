#pragma once

#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

namespace blockrig
{
class BlockInstance;

/// Saved blocks: a plug-in plus the settings you dialled in, one click away in
/// any rig.
///
/// Files on disk rather than a database, like the capture and IR libraries, so
/// favourites can be backed up, shared, and organised in Finder. One file per
/// favourite under …/BlockRig/Favorites, holding the plug-in's description and
/// its opaque state chunk.
class BlockFavorites final
{
public:
    BlockFavorites();

    struct Entry
    {
        juce::File file;
        juce::String name;
        juce::PluginDescription description;
    };

    juce::Array<Entry> getEntries() const;

    /// Saves a block's current settings under a name. Returns false when the
    /// block has no plug-in to ask.
    bool save(const BlockInstance& block, const juce::String& name);

    /// Reads a favourite's state chunk, for applying after the plug-in loads.
    juce::MemoryBlock loadState(const juce::File& file) const;

    void remove(const juce::File& file);

    juce::File getDirectory() const { return mDirectory; }

    std::function<void()> onChanged;

private:
    juce::File mDirectory;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockFavorites)
};

/// The block clipboard: copy a block with its settings, paste it anywhere,
/// including into a different rig.
///
/// A process-wide single item rather than a stack; nobody has ever wanted a
/// second clipboard slot for a guitar pedal.
class BlockClipboard final
{
public:
    void copy(const BlockInstance& block);
    bool hasContent() const { return mDescription.name.isNotEmpty(); }

    const juce::PluginDescription& getDescription() const { return mDescription; }
    const juce::MemoryBlock& getState() const { return mState; }
    bool wasBypassed() const { return mBypassed; }

private:
    juce::PluginDescription mDescription;
    juce::MemoryBlock mState;
    bool mBypassed = false;
};

} // namespace blockrig
