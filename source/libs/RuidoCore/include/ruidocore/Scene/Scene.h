#pragma once

#include "../Core/Pattern.h"

#include <string>
#include <utility>
#include <vector>

namespace ruidocore {

// A Part-level pattern bank: each part can have its own small library of
// pattern slots, independent from every other part's - this is what lets
// a Scene say "Part 1 -> slot 4" while Part 2 plays a completely
// different slot number. Recalling a Scene live must not interrupt
// playback - see the sample-accurate quantized-swap mechanism a host
// scheduler builds on top of Sequencer::setPart (which swaps a part's
// step data in place without resetting any part's cursor).
constexpr int kPatternSlotsPerPart = 4;

// Optional whole-Part parameter snapshot attached to a Scene, applied
// alongside the pattern-slot swap when the Scene is recalled. Deliberately
// generic ({paramId, value} pairs, not named fields) - this core library
// stays free of any specific synthesis-engine's parameter knowledge; the
// host application decides which parameters actually get captured/applied.
struct SceneVoiceSnapshot {
    bool enabled = false;
    std::vector<std::pair<int, int>> paramValues; // {paramId, value}
};

struct Scene {
    std::string name = "Scene";
    // Sized to match the Pattern's part count - a vector rather than a
    // fixed compile-time size, since voice count is meant to grow/shrink
    // over a project's life.
    std::vector<int> slotIndex;                       // which of the 4 slots (0-3) each part uses
    std::vector<SceneVoiceSnapshot> voiceSnapshots;    // one optional snapshot per part
};

// What a Song does once a step's `bars` have elapsed - a richer
// vocabulary than a single fixed "always advance to steps[i+1]",
// following the same clip/scene-launch conventions several DAWs and
// hardware sequencers already use. `Next` is the default and reproduces
// a plain linear sequence.
enum class NextAction { Next, Previous, First, Last, Random, Stop };

// One entry in a Song: play `sceneIndex` for `bars` bars, then resolve
// `onComplete` to pick what plays next. Bar = resolutionStepsPerBeat * 4
// steps (4/4 assumed).
struct SongStep {
    int sceneIndex = 0;
    int bars = 4;
    NextAction onComplete = NextAction::Next;
};

struct Song {
    std::vector<SongStep> steps;
    bool loop = true;
};

// How soon a queued Scene recall's PATTERN content actually takes effect -
// a device/session-wide quantize setting, following the same "pattern
// change quantization" convention several hardware sequencers and DAWs
// offer. Resolved sample-accurately inside a host scheduler's own
// step-generation loop for the pattern-slot swap itself; a Scene's
// separate VOICE snapshot (SceneVoiceSnapshot above) is a deliberate,
// documented asymmetry that a host may choose to apply immediately
// rather than waiting for the same boundary.
enum class SceneQuantize { Immediate, NextStep, NextBar, Next2Bars, Next4Bars };

} // namespace ruidocore
