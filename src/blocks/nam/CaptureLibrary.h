#pragma once

#include <functional>

#include <juce_core/juce_core.h>

namespace blockrig
{

/// The app's collection of NAM captures.
///
/// Every capture that successfully loads anywhere in the app is copied in, so
/// the library builds itself from use — no import step, and a rig restored on
/// another machine re-seeds it. Copies rather than references, because captures
/// arrive from Downloads folders that get cleaned out; a library of dangling
/// paths is worse than none.
///
/// Shared across every NAM block via juce::SharedResourcePointer.
class CaptureLibrary final
{
public:
    CaptureLibrary();

    struct Entry
    {
        juce::File file;
        juce::String name;
        /// Path relative to the library root ("Marshalls/High gain"), empty for
        /// the root. Folders are made and arranged in Finder - the library just
        /// reflects them, so organising captures needs no in-app file manager.
        juce::String folder;
        juce::Time added;
    };

    /// Newest first, which is also "most likely wanted next". Recurses into
    /// subfolders.
    juce::Array<Entry> getEntries() const;

    /// Copies a capture in. Content-deduplicated: re-loading the same file is a
    /// no-op, and a different capture under an already-used name gets a suffix
    /// rather than silently replacing what was there.
    void addCapture(const juce::File& source, const juce::String& subfolder = {});

    /// For captures that only exist as embedded rig state (the original file is
    /// gone) — the state carries the full JSON, which is the capture.
    void addCaptureJson(const juce::String& json, const juce::String& name,
                        const juce::String& subfolder = {});

    juce::File getDirectory() const { return mDirectory; }

    /// Fired on the message thread after the library gains a capture.
    std::function<void()> onChanged;

private:
    bool containsContent(const juce::String& hash) const;
    juce::File targetFileFor(const juce::String& name, const juce::String& hash,
                             const juce::String& subfolder) const;
    void noteAdded();

    juce::File mDirectory;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CaptureLibrary)
};

} // namespace blockrig
