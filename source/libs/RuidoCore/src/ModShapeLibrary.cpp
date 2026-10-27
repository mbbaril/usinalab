#include "ruidocore/Mod/ModShapeLibrary.h"

#include <algorithm>

namespace ruidocore {
namespace ModShapeLibrary {

namespace {
constexpr const char* kMagic = "RSHAPE01";
}

juce::File shapesFolder() {
    auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                      .getChildFile("Ruido")
                      .getChildFile("Mod Shapes");
    folder.createDirectory();
    return folder;
}

bool save(const juce::String& name, const std::vector<ModBreakpoint>& curve) {
    if (curve.size() < 2)
        return false;
    const juce::String safeName = juce::File::createLegalFileName(name.isEmpty() ? "Untitled" : name);
    const auto file = shapesFolder().getChildFile(safeName + ".rshape");

    juce::MemoryBlock block;
    juce::MemoryOutputStream stream(block, false);
    stream.writeString(kMagic);
    stream.writeString(name);
    stream.writeInt(static_cast<int>(curve.size()));
    for (const auto& bp : curve) {
        stream.writeFloat(bp.phase);
        stream.writeFloat(bp.value);
    }
    return file.replaceWithData(block.getData(), block.getSize());
}

std::vector<ModBreakpoint> load(const juce::File& file) {
    std::vector<ModBreakpoint> result;
    juce::MemoryBlock block;
    if (!file.loadFileAsData(block))
        return result;

    juce::MemoryInputStream stream(block, false);
    if (stream.readString() != kMagic)
        return result;
    stream.readString(); // name - the caller already has it from the file listing
    const int count = stream.readInt();
    if (count < 2 || count > 64)
        return result;
    result.resize(static_cast<size_t>(count));
    for (auto& bp : result) {
        bp.phase = stream.readFloat();
        bp.value = stream.readFloat();
    }
    return result;
}

juce::Array<juce::File> listShapes() {
    auto files = shapesFolder().findChildFiles(juce::File::findFiles, false, "*.rshape");
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileNameWithoutExtension().compareIgnoreCase(b.getFileNameWithoutExtension()) < 0;
    });
    return files;
}

} // namespace ModShapeLibrary
} // namespace ruidocore
