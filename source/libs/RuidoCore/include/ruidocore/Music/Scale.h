#pragma once

#include <array>
#include <vector>

namespace ruidocore {

// Pure music theory - no MIDI/CC knowledge at all (same separation as
// Rhythm/Euclidean.h). A real, musically-verified set (diatonic modes,
// pentatonics/blues, symmetric, jazz, and world/"exotic" scales), not an
// arbitrary long list.
enum class ScaleType {
    Chromatic,
    Major,
    NaturalMinor,
    HarmonicMinor,
    MajorPentatonic,
    MinorPentatonic,
    Blues,
    Dorian,
    Mixolydian,
    // Append scale entries; persisted numeric indices must remain stable.
    Phrygian,
    Lydian,
    Locrian,
    MelodicMinor,        // ascending/jazz melodic minor
    HarmonicMajor,
    MajorBlues,
    WholeTone,
    DiminishedWholeHalf, // aka "diminished"
    DiminishedHalfWhole, // aka "dominant diminished"
    HungarianMinor,      // "Gypsy minor"
    HungarianMajor,
    PhrygianDominant,    // aka Spanish/Freygish
    DoubleHarmonic,      // aka Byzantine/Arabic
    NeapolitanMinor,
    NeapolitanMajor,
    Enigmatic,
    Hirajoshi,           // Japanese pentatonic
    Insen,               // Japanese pentatonic
    Kumoi,               // Japanese pentatonic
    Egyptian,            // suspended pentatonic
    Persian,
    BebopDominant,       // jazz
    Altered              // aka Super Locrian, jazz
};

inline constexpr std::array<ScaleType, 32> kAllScaleTypes = {
    ScaleType::Chromatic,       ScaleType::Major,          ScaleType::NaturalMinor,     ScaleType::HarmonicMinor,
    ScaleType::MajorPentatonic, ScaleType::MinorPentatonic, ScaleType::Blues,           ScaleType::Dorian,
    ScaleType::Mixolydian,      ScaleType::Phrygian,       ScaleType::Lydian,           ScaleType::Locrian,
    ScaleType::MelodicMinor,    ScaleType::HarmonicMajor,  ScaleType::MajorBlues,       ScaleType::WholeTone,
    ScaleType::DiminishedWholeHalf, ScaleType::DiminishedHalfWhole, ScaleType::HungarianMinor, ScaleType::HungarianMajor,
    ScaleType::PhrygianDominant, ScaleType::DoubleHarmonic, ScaleType::NeapolitanMinor, ScaleType::NeapolitanMajor,
    ScaleType::Enigmatic,       ScaleType::Hirajoshi,      ScaleType::Insen,            ScaleType::Kumoi,
    ScaleType::Egyptian,        ScaleType::Persian,        ScaleType::BebopDominant,    ScaleType::Altered};

// root: 0=C, 1=C#, ... 11=B (matches MIDI pitch-class convention).
struct Scale {
    ScaleType type = ScaleType::Chromatic;
    int root = 0;
};

// Semitone offsets within one octave, sorted ascending, always includes 0.
const std::vector<int>& intervalsFor(ScaleType type);

// Snaps `semitoneFromRoot` (relative to MIDI note 0, i.e. absolute
// semitone - NOT relative to the scale's root) to the nearest semitone that
// belongs to the given scale. Works across any octave, positive or
// negative.
int quantizeSemitone(const Scale& scale, int semitoneFromRoot);

// Moves `noteFromRoot` to the ADJACENT scale degree in `direction` (+1 or
// -1) - unlike quantizeSemitone (nearest-neighbor snap), this always
// actually advances by exactly one degree, even when the scale's gaps are
// wider than 1 semitone. quantizeSemitone alone is unsuitable for a
// "nudge by one wheel-click" gesture: e.g. a 2-semitone scale gap makes
// note+1 land equidistant between the current degree and the next one, so
// nearest-neighbor rounding can snap right back to the SAME note every
// time - the wheel appears stuck . `noteFromRoot` need not already be
// exactly on-scale (it's re-quantized first internally).
int stepScaleDegree(const Scale& scale, int noteFromRoot, int direction);

const char* toString(ScaleType type);
const char* noteName(int pitchClass); // 0-11 -> "C".."B"

} //
