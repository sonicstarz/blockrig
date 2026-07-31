#include "state/Setlist.h"

#include "state/RigFiles.h"

namespace blockrig
{
namespace
{
juce::File rigsFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("BlockRig")
        .getChildFile("Rigs");
}
} // namespace

juce::File Setlist::getFolder()
{
    auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                      .getChildFile("BlockRig")
                      .getChildFile("Setlists");
    folder.createDirectory();
    return folder;
}

juce::Array<juce::File> Setlist::findAll()
{
    auto files = getFolder().findChildFiles(juce::File::findFiles, false, "*" + getFileExtension());
    files.sort();
    return files;
}

bool Setlist::loadFrom(const juce::File& file)
{
    const auto parsed = juce::JSON::parse(file.loadFileAsString());

    if (!parsed.isObject())
        return false;

    name = parsed.getProperty("name", file.getFileNameWithoutExtension()).toString();
    rigNames.clear();

    if (const auto* array = parsed.getProperty("rigs", {}).getArray())
        for (const auto& entry : *array)
            rigNames.add(entry.toString());

    return true;
}

bool Setlist::saveTo(const juce::File& file) const
{
    juce::Array<juce::var> rigs;
    for (const auto& rigName : rigNames)
        rigs.add(rigName);

    auto* object = new juce::DynamicObject();
    object->setProperty("name", name);
    object->setProperty("rigs", rigs);

    return file.replaceWithText(juce::JSON::toString(juce::var(object)));
}

juce::File Setlist::getRigFile(int index) const
{
    if (!juce::isPositiveAndBelow(index, rigNames.size()))
        return {};

    return rigsFolder().getChildFile(rigNames[index] + juce::String(rigfiles::kFileExtension));
}

int Setlist::indexOfRig(const juce::File& rigFile) const
{
    return rigNames.indexOf(rigFile.getFileNameWithoutExtension());
}

} // namespace blockrig
