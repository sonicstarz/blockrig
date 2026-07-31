#pragma once

#include <juce_core/juce_core.h>

namespace blockrig
{

/// An ordered list of rigs for a show.
///
/// Stores rig file NAMES rather than absolute paths: a setlist that survives
/// moving the BlockRig folder, or being handed to the other guitarist, is worth
/// more than one that can point at a rig outside the rigs folder. A rig that
/// has been renamed or deleted shows as missing rather than silently vanishing.
class Setlist final
{
public:
    static juce::File getFolder();
    static juce::String getFileExtension() { return ".blockset"; }

    /// All setlists on disk, by name.
    static juce::Array<juce::File> findAll();

    bool loadFrom(const juce::File& file);
    bool saveTo(const juce::File& file) const;

    juce::String name;
    juce::StringArray rigNames;

    /// The rig file for an entry, or a non-existent File when it is missing.
    juce::File getRigFile(int index) const;

    int indexOfRig(const juce::File& rigFile) const;
};

} // namespace blockrig
