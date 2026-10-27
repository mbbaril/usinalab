#pragma once

#include "../Core/Pattern.h"

#include <array>
#include <cstdint>
#include <random>
#include <string>

namespace ruidocore {

// Styles offered by the Generator, each backed by sourced rhythmic
// conventions (see Generator.cpp's styleGrid()/generate() comments for the
// specific trait and source each style encodes), not just invented weight
// tables. "Breakcore" was deliberately left out: its identity is
// break-slicing/reordering, not a groove template - a flat weight array
// would not sound authentic, so it's a documented gap rather than a faked
// style.
enum class GeneratorStyle {
    Drum,
    Industrial,
    DrumAndNoise,
    PowerNoise,
    Machine,
    Glitch,
    Experimental,
    Ebm,
    Gabber,
    DubTechno,
};

// High-level macro controls. All 0..1 unless noted.
struct GeneratorParams {
    GeneratorStyle style = GeneratorStyle::DrumAndNoise;
    float density = 0.5f;       // overall likelihood of steps being active
    float regularity = 0.5f;    // 1 = locked to the style's grid, 0 = free/random
    float repetition = 0.5f;    // 1 = motif repeats verbatim, 0 = fully re-rolled each block
    float burst = 0.4f;         // likelihood/intensity of stutter/burst clusters
    float syncopation = 0.3f;   // shifts weight from on-grid to off-grid steps
    // Accent CONTRAST, independent of density (which only ever decides
    // ON/OFF). 0 = every active step lands at the same mid tier (uniform);
    // 1 = full spread from the style grid's own weight. Applied as
    // accentAmount = 0.5 + (gridWeight - 0.5) * dynamics.
    float dynamics = 1.0f;
    // Microtiming humanization for ANY style, not just Glitch. 0 =
    // perfectly quantized; 1 = up to +/-8ms of continuous per-hit jitter,
    // subtler than Glitch's own deliberate displacement, stacking
    // additively on top of it for Glitch itself.
    float feel = 0.0f;
    float swingPercent = 0.0f;  // 0-100, delays odd-numbered steps
    int length = 16;
    double bpm = 120.0;
    std::uint32_t seed = 1;     // same seed + params -> same pattern, always
};

// Shared list for UI/CLI to enumerate styles consistently without
// duplicating the enum-to-name mapping.
constexpr std::array<GeneratorStyle, 10> kAllGeneratorStyles{
    GeneratorStyle::Drum,       GeneratorStyle::Industrial,   GeneratorStyle::DrumAndNoise,
    GeneratorStyle::PowerNoise, GeneratorStyle::Machine,      GeneratorStyle::Glitch,
    GeneratorStyle::Experimental, GeneratorStyle::Ebm,       GeneratorStyle::Gabber,
    GeneratorStyle::DubTechno,
};
const char* toString(GeneratorStyle style);

// Axis bundle shared by factory presets and user-saved presets - literally
// what GeneratorParams exposes to a person turning knobs, minus
// seed/length/bpm (not part of a preset's "character"). GeneratorStyle
// stays in here as an internal detail (it still picks which pattern-
// shaping grid gets used, see styleGrid() in Generator.cpp) - it's just no
// longer the thing a user picks by genre name.
struct GeneratorPresetValues {
    GeneratorStyle style = GeneratorStyle::DrumAndNoise;
    float density = 0.5f;
    float regularity = 0.5f;
    float repetition = 0.5f;
    float burst = 0.4f;
    float syncopation = 0.3f;
    float dynamics = 1.0f;
    float feel = 0.0f;
    float swingPercent = 0.0f; // 0-100, matches Pattern::swingPercent's own range
};

struct GeneratorPreset {
    const char* name;
    GeneratorPresetValues values;
};
constexpr int kFactoryGeneratorPresetCount = 10;
const std::array<GeneratorPreset, kFactoryGeneratorPresetCount>& factoryGeneratorPresets();

// User-saved presets - a name plus the exact same axis bundle a factory
// preset carries, captured from the knobs' live values at Save time.
// std::string, not a UI-framework string type - this header stays free of
// any GUI-toolkit dependency.
struct GeneratorUserPreset {
    std::string name;
    GeneratorPresetValues values;
};

// Generates a full drum-kit-shaped Pattern in one shot: 6 roles (kick-like
// anchor, snare-like backbeat, dense hat/texture layer, syncopated hit,
// sparse accent/fx, burst/glitch layer), matching a conventional small
// drum kit rather than an arbitrary part count - see kPartRoles in
// Generator.cpp. Pure function of its inputs: no RNG state escapes this
// call, so the same GeneratorParams always produce the exact same Pattern.
// What a host changes about GENERATE (2026-09-16). Three Volca-family
// copies of this generator differed only in these: how many lanes and in
// which roles, how many steps a Part stores, whether it writes velocity,
// how it walks a melodic line, and what it bakes after a Part is built.
// Every switch that is off draws nothing from the random stream, so a
// profile reproduces exactly what a copy written without that feature did.

// One lane's character: how busy it is relative to the others, and how far
// the style grid is rotated for it.
struct GeneratorRole {
    float densityMultiplier;
    int rotation;
};

// A melodic line walked across one Part's enabled steps. The host owns it
// and its state (a scale, a window, where the line is). beginPart runs once
// per Part, after its rhythm is decided; step once per enabled step.
class GeneratorPitchWalk {
public:
    virtual ~GeneratorPitchWalk() = default;
    virtual void beginPart(std::mt19937& rng, const GeneratorParams& params) = 0;
    virtual void step(std::mt19937& rng, const GeneratorParams& effective, StepEvent& ev) = 0;
};

struct GeneratorProfile {
    // Null = the six drum-kit roles this generator was written with.
    const GeneratorRole* roles = nullptr;
    int roleCount = 0;
    // Steps each Part stores; 0 = params.length. Steps past length stay
    // disabled - storage, not pattern.
    int storageSteps = 0;
    // Draw and write note velocity (and the burst's velocity floor).
    bool velocity = true;
    // EBM's secondary voice (lane 3) gets small bass-line semitone offsets.
    bool secondaryVoiceOffsets = true;
    GeneratorPitchWalk* pitchWalk = nullptr;
    // Where the walk runs within a step: first thing, or after the accent
    // (and then only where secondaryVoiceOffsets did not already set pitch).
    enum class WalkAt { BeforeDynamics, AfterAccent };
    WalkAt walkAt = WalkAt::AfterAccent;
    // Runs on each finished Part - e.g. bake the accent into the host's own
    // parameter locks.
    void (*afterPart)(int partIndex, Part& part) = nullptr;
};

class Generator {
public:
    static Pattern generate(const GeneratorParams& params, const GeneratorProfile& profile = {});
};

} // namespace ruidocore
