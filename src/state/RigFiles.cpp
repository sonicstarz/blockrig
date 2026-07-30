#include "state/RigFiles.h"

#include "BlockRigProcessor.h"

namespace blockrig::rigfiles
{

juce::File getDefaultDirectory()
{
    auto directory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                         .getChildFile("BlockRig Rigs");
    directory.createDirectory();
    return directory;
}

bool save(BlockRigProcessor& processor, const juce::File& file, juce::String& errorOut)
{
    const auto rig = rigstate::toValueTree(processor);

    if (!rig.isValid())
    {
        errorOut = "There is nothing to save.";
        return false;
    }

    auto target = file;
    if (!target.hasFileExtension(kFileExtension))
        target = target.withFileExtension(kFileExtension);

    target.getParentDirectory().createDirectory();

    // Write to a temporary file and swap, so a failure part-way through cannot
    // destroy a rig that was already on disk.
    juce::TemporaryFile temporary(target);

    {
        juce::FileOutputStream stream(temporary.getFile());

        if (!stream.openedOk())
        {
            errorOut = "Could not write to " + target.getFullPathName();
            return false;
        }

        rig.writeToStream(stream);
    }

    if (!temporary.overwriteTargetFileWithTemporary())
    {
        errorOut = "Could not replace " + target.getFullPathName();
        return false;
    }

    return true;
}

void load(BlockRigProcessor& processor, const juce::File& file,
          std::function<void(rigstate::RestoreResult, juce::String)> onFinished)
{
    if (!file.existsAsFile())
    {
        if (onFinished)
            onFinished({}, "No such file: " + file.getFullPathName());
        return;
    }

    juce::FileInputStream stream(file);

    if (!stream.openedOk())
    {
        if (onFinished)
            onFinished({}, "Could not read " + file.getFullPathName());
        return;
    }

    const auto rig = juce::ValueTree::readFromStream(stream);

    if (!rig.isValid())
    {
        if (onFinished)
            onFinished({}, file.getFileName() + " is not a BlockRig rig.");
        return;
    }

    rigstate::restore(processor, rig, [onFinished](rigstate::RestoreResult result) {
        if (onFinished)
            onFinished(std::move(result), {});
    });
}

} // namespace blockrig::rigfiles
