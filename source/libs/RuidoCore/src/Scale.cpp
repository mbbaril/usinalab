#include "ruidocore/Music/Scale.h"

#include <cstdlib>
#include <limits>

namespace ruidocore {

const std::vector<int>& intervalsFor(ScaleType type) {
    static const std::vector<int> chromatic = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    static const std::vector<int> major = {0, 2, 4, 5, 7, 9, 11};
    static const std::vector<int> naturalMinor = {0, 2, 3, 5, 7, 8, 10};
    static const std::vector<int> harmonicMinor = {0, 2, 3, 5, 7, 8, 11};
    static const std::vector<int> majorPentatonic = {0, 2, 4, 7, 9};
    static const std::vector<int> minorPentatonic = {0, 3, 5, 7, 10};
    static const std::vector<int> blues = {0, 3, 5, 6, 7, 10};
    static const std::vector<int> dorian = {0, 2, 3, 5, 7, 9, 10};
    static const std::vector<int> mixolydian = {0, 2, 4, 5, 7, 9, 10};
    static const std::vector<int> phrygian = {0, 1, 3, 5, 7, 8, 10};
    static const std::vector<int> lydian = {0, 2, 4, 6, 7, 9, 11};
    static const std::vector<int> locrian = {0, 1, 3, 5, 6, 8, 10};
    static const std::vector<int> melodicMinor = {0, 2, 3, 5, 7, 9, 11};
    static const std::vector<int> harmonicMajor = {0, 2, 4, 5, 7, 8, 11};
    static const std::vector<int> majorBlues = {0, 2, 3, 4, 7, 9};
    static const std::vector<int> wholeTone = {0, 2, 4, 6, 8, 10};
    static const std::vector<int> diminishedWholeHalf = {0, 2, 3, 5, 6, 8, 9, 11};
    static const std::vector<int> diminishedHalfWhole = {0, 1, 3, 4, 6, 7, 9, 10};
    static const std::vector<int> hungarianMinor = {0, 2, 3, 6, 7, 8, 11};
    static const std::vector<int> hungarianMajor = {0, 3, 4, 6, 7, 9, 10};
    static const std::vector<int> phrygianDominant = {0, 1, 4, 5, 7, 8, 10};
    static const std::vector<int> doubleHarmonic = {0, 1, 4, 5, 7, 8, 11};
    static const std::vector<int> neapolitanMinor = {0, 1, 3, 5, 7, 8, 11};
    static const std::vector<int> neapolitanMajor = {0, 1, 3, 5, 7, 9, 11};
    static const std::vector<int> enigmatic = {0, 1, 4, 6, 8, 10, 11};
    static const std::vector<int> hirajoshi = {0, 2, 3, 7, 8};
    static const std::vector<int> insen = {0, 1, 5, 7, 10};
    static const std::vector<int> kumoi = {0, 2, 3, 7, 9};
    static const std::vector<int> egyptian = {0, 2, 5, 7, 10};
    static const std::vector<int> persian = {0, 1, 4, 5, 6, 8, 11};
    static const std::vector<int> bebopDominant = {0, 2, 4, 5, 7, 9, 10, 11};
    static const std::vector<int> altered = {0, 1, 3, 4, 6, 8, 10};

    switch (type) {
        case ScaleType::Chromatic: return chromatic;
        case ScaleType::Major: return major;
        case ScaleType::NaturalMinor: return naturalMinor;
        case ScaleType::HarmonicMinor: return harmonicMinor;
        case ScaleType::MajorPentatonic: return majorPentatonic;
        case ScaleType::MinorPentatonic: return minorPentatonic;
        case ScaleType::Blues: return blues;
        case ScaleType::Dorian: return dorian;
        case ScaleType::Mixolydian: return mixolydian;
        case ScaleType::Phrygian: return phrygian;
        case ScaleType::Lydian: return lydian;
        case ScaleType::Locrian: return locrian;
        case ScaleType::MelodicMinor: return melodicMinor;
        case ScaleType::HarmonicMajor: return harmonicMajor;
        case ScaleType::MajorBlues: return majorBlues;
        case ScaleType::WholeTone: return wholeTone;
        case ScaleType::DiminishedWholeHalf: return diminishedWholeHalf;
        case ScaleType::DiminishedHalfWhole: return diminishedHalfWhole;
        case ScaleType::HungarianMinor: return hungarianMinor;
        case ScaleType::HungarianMajor: return hungarianMajor;
        case ScaleType::PhrygianDominant: return phrygianDominant;
        case ScaleType::DoubleHarmonic: return doubleHarmonic;
        case ScaleType::NeapolitanMinor: return neapolitanMinor;
        case ScaleType::NeapolitanMajor: return neapolitanMajor;
        case ScaleType::Enigmatic: return enigmatic;
        case ScaleType::Hirajoshi: return hirajoshi;
        case ScaleType::Insen: return insen;
        case ScaleType::Kumoi: return kumoi;
        case ScaleType::Egyptian: return egyptian;
        case ScaleType::Persian: return persian;
        case ScaleType::BebopDominant: return bebopDominant;
        case ScaleType::Altered: return altered;
    }
    return chromatic;
}

int quantizeSemitone(const Scale& scale, int semitoneFromRoot) {
    const auto& intervals = intervalsFor(scale.type);
    if (intervals.empty())
        return semitoneFromRoot;

    const int rel = semitoneFromRoot - scale.root;
    // Floor division towards -infinity, so negative `rel` octaves work too.
    const int baseOctave = (rel >= 0) ? (rel / 12) : ((rel - 11) / 12);

    int best = 0;
    int bestDist = std::numeric_limits<int>::max();
    // +/-1 octave window around `rel` is always enough: no scale interval
    // is more than 11 semitones from an octave boundary.
    for (int octaveOffset = -1; octaveOffset <= 1; ++octaveOffset) {
        const int octave = baseOctave + octaveOffset;
        for (int interval : intervals) {
            const int candidate = octave * 12 + interval;
            const int dist = std::abs(candidate - rel);
            if (dist < bestDist) {
                bestDist = dist;
                best = candidate;
            }
        }
    }
    return best + scale.root;
}

int stepScaleDegree(const Scale& scale, int noteFromRoot, int direction) {
    const auto& intervals = intervalsFor(scale.type);
    if (intervals.empty() || direction == 0)
        return noteFromRoot + direction;

    // Start from an actual scale degree so stepping is well-defined, even
    // if the caller passed a note that isn't exactly on-scale.
    const int startNote = quantizeSemitone(scale, noteFromRoot);
    const int rel = startNote - scale.root;
    const int octave = (rel >= 0) ? (rel / 12) : ((rel - 11) / 12);
    const int pitchClass = rel - octave * 12; // 0-11

    const int n = static_cast<int>(intervals.size());
    int idx = 0;
    for (int i = 0; i < n; ++i) {
        if (intervals[static_cast<size_t>(i)] == pitchClass) {
            idx = i;
            break;
        }
    }

    idx += (direction > 0 ? 1 : -1);
    int newOctave = octave;
    if (idx >= n) {
        idx -= n;
        newOctave += 1;
    } else if (idx < 0) {
        idx += n;
        newOctave -= 1;
    }
    return newOctave * 12 + intervals[static_cast<size_t>(idx)] + scale.root;
}

const char* toString(ScaleType type) {
    switch (type) {
        case ScaleType::Chromatic: return "Off (Chromatic)";
        case ScaleType::Major: return "Major (Ionian)";
        case ScaleType::NaturalMinor: return "Natural Minor (Aeolian)";
        case ScaleType::HarmonicMinor: return "Harmonic Minor";
        case ScaleType::MajorPentatonic: return "Major Pentatonic";
        case ScaleType::MinorPentatonic: return "Minor Pentatonic";
        case ScaleType::Blues: return "Blues";
        case ScaleType::Dorian: return "Dorian";
        case ScaleType::Mixolydian: return "Mixolydian";
        case ScaleType::Phrygian: return "Phrygian";
        case ScaleType::Lydian: return "Lydian";
        case ScaleType::Locrian: return "Locrian";
        case ScaleType::MelodicMinor: return "Melodic Minor";
        case ScaleType::HarmonicMajor: return "Harmonic Major";
        case ScaleType::MajorBlues: return "Major Blues";
        case ScaleType::WholeTone: return "Whole Tone";
        case ScaleType::DiminishedWholeHalf: return "Diminished (Whole-Half)";
        case ScaleType::DiminishedHalfWhole: return "Diminished (Half-Whole)";
        case ScaleType::HungarianMinor: return "Hungarian Minor (Gypsy)";
        case ScaleType::HungarianMajor: return "Hungarian Major";
        case ScaleType::PhrygianDominant: return "Phrygian Dominant (Spanish)";
        case ScaleType::DoubleHarmonic: return "Double Harmonic (Byzantine)";
        case ScaleType::NeapolitanMinor: return "Neapolitan Minor";
        case ScaleType::NeapolitanMajor: return "Neapolitan Major";
        case ScaleType::Enigmatic: return "Enigmatic";
        case ScaleType::Hirajoshi: return "Hirajoshi (Japanese)";
        case ScaleType::Insen: return "Insen (Japanese)";
        case ScaleType::Kumoi: return "Kumoi (Japanese)";
        case ScaleType::Egyptian: return "Egyptian";
        case ScaleType::Persian: return "Persian";
        case ScaleType::BebopDominant: return "Bebop Dominant";
        case ScaleType::Altered: return "Altered (Super Locrian)";
    }
    return "Chromatic";
}

const char* noteName(int pitchClass) {
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    pitchClass = ((pitchClass % 12) + 12) % 12;
    return names[pitchClass];
}

} // namespace ruidocore
