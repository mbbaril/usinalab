#pragma once

#include "../Core/Mod.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace ruidocore {

// A saved Custom envelope shape, shareable across every host plugin built
// on this library. A ModBreakpoint curve (phase/value pairs, 0..1 each)
// carries no parameter-id or synth-specific data at all, so it's
// naturally portable - a shape drawn in one host application is exactly
// as meaningful in any other, no translation needed.
//
// Stored as plain files in a shared folder OUTSIDE any single host's own
// preset system, so every host application reading/writing the same
// folder sees the same shapes immediately - no sync mechanism needed
// beyond "write to the same place."
//
// This is the one core module that depends on JUCE (juce::File for
// filesystem access) - everything else in this library is
// framework-agnostic.
namespace ModShapeLibrary {

// A per-user shared shapes folder (juce::File::getSpecialLocation on
// every platform this targets).
juce::File shapesFolder();

// One .rshape file per saved curve - `name` becomes both the filename
// (sanitised) and the label shown in a Load Shape browser.
bool save(const juce::String& name, const std::vector<ModBreakpoint>& curve);

// Empty on failure (corrupt file, wrong magic, etc.) - never partially
// filled.
std::vector<ModBreakpoint> load(const juce::File& file);

// Every .rshape file currently in shapesFolder(), sorted by name.
juce::Array<juce::File> listShapes();

} // namespace ModShapeLibrary

} // namespace ruidocore
