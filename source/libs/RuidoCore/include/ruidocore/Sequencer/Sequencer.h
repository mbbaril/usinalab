#pragma once

#include "../Core/Pattern.h"

#include <random>
#include <vector>

namespace ruidocore {

// One part's step, resolved and ready to hand to the dispatch/output layer.
struct FiredStep {
    int partIndex = 0;
    int outputId = 1;
    int stepIndex = 0; // the step's index within this part's own pattern
    StepEvent event;   // copied out - small struct, keeps ownership simple
};

// Owns a Pattern's live playback state: each part's step cursor (which
// advances according to its own Direction and length - independent
// per-part polymeter) and performance-only mute/solo overrides.
// Deliberately knows nothing about MIDI, audio, or timing/scheduling, and
// does not evaluate step probability - that is an execution-time concern
// owned by whoever consumes tick().
//
// Part count is read from the Pattern passed to the constructor - internal
// state vectors are sized to match, not fixed at compile time.
class Sequencer {
public:
    // A host's STRUCTURAL facts about a Part - the ones that follow from
    // where the Part sits, not from what a user edited: which output a
    // lane addresses, which note a lane plays when a step names none.
    // Carrying them as ordinary data let copies lose them (a Part pasted
    // or recalled from a bank kept the channel of wherever it came from,
    // and every hit went to the wrong voice). So the Sequencer calls the
    // host's stamp on EVERY Part as it enters - constructor, setPattern,
    // addPart, setPart - and again on each Part whose index removePart
    // shifts. What the stamp derives cannot drift, whatever path a Part
    // took. Null = the Part is taken as it is.
    //
    // A plain function pointer on purpose: setPart runs on the audio
    // thread when a queued Scene swap lands, so the stamp must be cheap
    // and must not allocate, and nothing here may.
    using PartStamp = void (*)(int partIndex, Part& part);

    // A Part's clock, as something OUTSIDE the Pattern may temporarily
    // want it (2026-09-17, user on what GLITCH should be: "usar multiply
    // e divide de clock, quebrar a assinatura de tempo").
    //
    // The multiplier and divider live on the Part, so a disruptor that
    // wanted to bend them had only two bad options: write into the
    // Pattern (an edit, undoable, and visible in the UI as if the user
    // had done it) or not bend them at all. This is the third: the
    // sequencer asks, every pulse, whether anything wants this Part's
    // clock to read differently right now. Nothing stored, nothing
    // edited, and the Pattern is still the only thing that gets saved.
    using ClockWarp = void (*)(void* context, int partIndex, int& multiplier, int& divider);
    void setClockWarp(ClockWarp fn, void* context) {
        clockWarp = fn;
        clockWarpContext = context;
    }

    explicit Sequencer(Pattern patternIn, PartStamp stamp = nullptr);

    // Advances every part by one step and returns the ones that are
    // enabled, unmuted, and audible under the current solo state. Call
    // once per global step tick.
    std::vector<FiredStep> tick();

    // THE REAL-TIME FORM (2026-09-22). Measured on the Volca Drum: the
    // audio thread allocated 5.2 times per block with a pattern playing.
    // An average cost in microseconds hides what that risks - an
    // allocation can block on the heap lock under load, which is how a
    // plugin that measures 0.03% of a core still clicks.
    //
    // `out` is a buffer the CALLER owns and keeps between blocks. It is
    // grown and never shrunk, and each slot's StepEvent keeps its own
    // `locks` capacity, so a steady pattern settles into allocating
    // nothing at all. Returns how many entries at the FRONT of `out` are
    // valid this tick - the rest are stale and must not be read. It does
    // not resize down, because that is exactly what would throw the
    // capacity away again.
    std::size_t tick(std::vector<FiredStep>& out);

    void setMute(int partIndex, bool isMuted);
    void setSolo(int partIndex, bool isSoloed);
    void resetCursors();

    // Replaces the WHOLE pattern, including growing or shrinking the part
    // count - voice count is meant to be changeable during a project's
    // life (add/remove a lane), not fixed once at construction. Every
    // part's cursor/direction/loop-count state resets (this is a full
    // structural replace, unlike setPart()'s single-part non-disruptive
    // swap below) - if you only need to change ONE part's content without
    // touching the others' playback position, use setPart() instead.
    void setPattern(Pattern newPattern);
    // Appends one new part at the end, its own state starting fresh
    // ("not started yet") - every other part's cursor/position is
    // untouched, so adding a voice mid-performance doesn't disrupt what's
    // already playing.
    void addPart(Part part);
    // Removes one part and its associated playback state. Every
    // remaining part's own cursor/position is untouched (indices above
    // the removed one shift down by one, same as std::vector::erase).
    void removePart(int partIndex);

    // Swaps ONE part's step data in place, WITHOUT touching any part's
    // cursor (including the swapped part's own) - this is what makes a
    // pattern-slot/Scene recall non-disruptive: everything keeps advancing
    // exactly where it was, only the note content differs from here on. If
    // the new part is shorter than the current cursor position, the cursor
    // is clamped so the next tick() lands in range instead of reading out
    // of bounds.
    void setPart(int partIndex, Part newPart);

    // setPart's real-time form (2026-10-21, found writing Usina's sequencer).
    // setPart MOVES the new Part in, and a move-assignment frees the old
    // Part's storage on the spot - on the audio thread, where this class's
    // own contract says nothing may allocate; a free takes the same heap
    // lock an allocation does. This SWAPS instead: the Sequencer gets the
    // new Part's contents, `part` gets the old Part's, nothing is allocated
    // or freed here, and the caller destroys the old one wherever it likes
    // (off the audio thread). Same cursor clamp and stamp as setPart.
    void exchangePart(int partIndex, Part& part);

    // The non-part half of what the constructor does: replaces the
    // pattern-wide scalars and nothing else - no cursor, no mute, no solo,
    // no loop count. Looped with setPart() this is a WHOLE-pattern swap
    // that keeps the transport and the performance state alive, which is
    // what Undo/Redo needs; building a fresh Sequencer throws both away.
    void setPatternGlobals(double bpm, int resolutionStepsPerBeat, float swingPercent);
    void setPatternGlobals(const Pattern& source) {
        setPatternGlobals(source.bpm, source.resolutionStepsPerBeat, source.swingPercent);
    }

    const Pattern& getPattern() const { return pattern; }

    // How many full passes this part has made since the last
    // resetCursors() - Trig Conditions' Ratio/FirstLoop types need this.
    // Increments when the cursor lands back on step 0 (not counting the
    // very first tick). Approximate for Reverse/PingPong/Random directions
    // (there's no single universally "correct" loop-boundary definition
    // for those) but exact for the common Forward case.
    int loopCountForPart(int partIndex) const {
        return (partIndex >= 0 && partIndex < static_cast<int>(loopCount.size())) ? loopCount[static_cast<size_t>(partIndex)] : 0;
    }

    // The real, always-advancing cursor, independent of whether the step
    // it's currently on is enabled. tick()'s returned FiredStep list only
    // contains ENABLED steps by design, so a UI playhead driven purely
    // from FiredStep freezes on any gap in the pattern instead of visibly
    // advancing through it - this getter is the fix.
    int cursorForPart(int partIndex) const {
        return (partIndex >= 0 && partIndex < static_cast<int>(cursor.size())) ? cursor[static_cast<size_t>(partIndex)] : -1;
    }
    bool isMuted(int partIndex) const {
        return (partIndex >= 0 && partIndex < static_cast<int>(muted.size())) && muted[static_cast<size_t>(partIndex)];
    }
    bool isSoloed(int partIndex) const {
        return (partIndex >= 0 && partIndex < static_cast<int>(soloed.size())) && soloed[static_cast<size_t>(partIndex)];
    }

private:
    int advanceCursor(int partIndex);
    void stampFrom(size_t firstIndex);

    Pattern pattern;
    PartStamp stamp = nullptr;
    ClockWarp clockWarp = nullptr;
    void* clockWarpContext = nullptr;
    std::vector<int> cursor;           // -1 = "not started yet"
    std::vector<int> pingPongDir;      // +1 or -1
    std::vector<int> loopCount;        // see loopCountForPart()
    // Per-part clock rate accumulator - a rational pulse counter, NOT a
    // sub-tick timer: every global tick() call adds Part::clockMultiplier,
    // then advanceCursor() fires once per whole Part::clockDivider
    // accumulated, so a part can fire 0, 1, or several times within one
    // tick() call without changing the shared global tick's own
    // resolution. See tick()'s implementation.
    std::vector<int> pulseAccumulator;
    std::vector<bool> muted;
    std::vector<bool> soloed;
    std::mt19937 rng{std::random_device{}()};
};

} // namespace ruidocore
