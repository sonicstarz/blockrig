#pragma once

#include <functional>

#include <juce_core/juce_core.h>

namespace blockrig
{

/// The app's collection of impulse responses, built the same way as the capture
/// library: anything that loads successfully is copied in, organised by
/// subfolders the user makes in Finder.
///
/// Separate from CaptureLibrary rather than generalised over it, because the two
/// differ in the ways that matter: IRs are binary audio files of a few hundred
/// samples, captures are JSON. Sharing an abstraction would mean hiding both
/// behind a lowest common denominator to save maybe thirty lines.
class IrLibrary final
{
public:
    IrLibrary();

    struct Entry
    {
        juce::File file;
        juce::String name;
        /// Path relative to the library root; empty for the root.
        juce::String folder;
        juce::Time added;
    };

    juce::Array<Entry> getEntries() const;

    /// Copies an IR in, deduplicated by content hash carried in the filename.
    void addIr(const juce::File& source, const juce::String& subfolder = {});

    juce::File getDirectory() const { return mDirectory; }

    /// What the file chooser and the Downloads watcher accept.
    static juce::String getWildcard() { return "*.wav;*.aif;*.aiff"; }

    std::function<void()> onChanged;

private:
    juce::File mDirectory;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrLibrary)
};

} // namespace blockrig
