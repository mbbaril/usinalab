// SPDX-License-Identifier: GPL-3.0-only
// Usina - the sequencer's player (docs/SEQUENCIADOR.md): the family's sequencer engine (RuidoCore's Sequencer: per-lane
// length, direction and clock, polymeter, trig conditions; its generators for euclid, Turing, mutation, evolution)
// clocked by the host's beat position, its steps turned into
//   - notes, sample-accurate: probability, trig condition, ratchet, accent, microtiming and swing applied as each step
//     plays; EVERY note-on paired with its note-off (at the step's gate length, before a repeat of the same note, at a
//     stop or a jump of the transport);
//   - the modulation lanes' values (lanes 1..4: the matrix's SEQ 1..4 sources; a step sets its lane's value, a step off
//     holds it).
// The clock: the host's ppqPosition read every block (rule 10), the step grid recomputed from it at any discontinuity
// (a loop, a jump, the first block) and otherwise followed sample by sample; a bad ppq or bpm (NaN, absurd) never
// reaches the arithmetic; at most 64 steps a block (a host's stall never fires a burst). Lessons of the family's
// schedulers, measured there live.
// Editing while it plays: a lane edited on the message thread travels to the audio thread through a lock-free mailbox
// and enters with RuidoCore's exchangePart - no allocation, no free on the audio thread; the old lane comes back and is
// freed on the message thread (collect()). Plain C++, no JUCE.
#pragma once
#include <ruidocore/Sequencer/Sequencer.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace usina::seq {

constexpr int kNoteLane = 0;
constexpr int kModLanes = 4;
// the percussion's lanes (22/10; the user: "podemos ter 4 pistas de 16 passos assim, seria otimo em um modo drums"):
// after the others, so a session saved with five lanes reads as before (its drums empty). Each a note of the voice
// (its base note: a drum, a plate, a can - tuned apart), its steps' velocity the hit's strength
constexpr int kDrumLanes = 4;
constexpr int kFirstDrumLane = 1 + kModLanes;
constexpr int kLanes = 1 + kModLanes + kDrumLanes;
constexpr int kDrumBaseNotes[kDrumLanes] = { 36, 41, 48, 55 };
inline bool isNoteLane(int lane) { return lane == kNoteLane || (lane >= kFirstDrumLane && lane < kLanes); }

struct Event {
    int sample = 0;                                      // within the block
    bool on = false;
    int note = 60;
    int velocity = 100;
};

struct Transport {
    double bpm = 120.0;
    double ppq = 0.0;
    bool ppqValid = false;
    bool playing = false;
};

// a pattern as the sequencer starts with: a note lane and four modulation lanes, 16 steps each, nothing on
ruidocore::Pattern defaultPattern();

class Player {
public:
    static constexpr int kChordNotes = 16;
    // Zero duration inherits the legacy step gate. Positive values are lane steps, independent per pitch.
    struct Chord { int count = 0; std::array<int, kChordNotes> notes {}; std::array<float, kChordNotes> durations {}; };
    using ChordLane = std::array<Chord, 64>;
    Player();
    ~Player();

    void prepare(double sampleRate);
    // the whole pattern at once (a session loaded, prepare): NOT while process() can run - the plugin calls it with
    // processing suspended
    void setPatternNow(const ruidocore::Pattern& p);
    // one lane's new content while it plays (message thread): false when the mailbox is full (try again later)
    bool submitLane(int lane, const ruidocore::Part& part, const ChordLane* chords = nullptr);
    // the grid and the swing (any thread; taken at the next block)
    void setGlobals(int stepsPerBeat, float swingPercent);
    // message thread: frees what the audio thread handed back
    void collect();

    // audio thread: one host block. Returns how many events were written to out (sorted by sample)
    int process(int numSamples, const Transport& t, Event* out, int maxOut);
    float laneValue(int modLane) const;                  // 0..1 (lane 0..3: SEQ 1..4), as of the last block's end
    int cursor(int lane) const;                          // the lane's real cursor (any thread; -1: not playing)
    void setMuted(int lane, bool m);                     // (any thread: a lane silent, its cursor running)
    bool muted(int lane) const;

private:
    struct Pending {
        double at;
        bool on;
        int note, velocity;
        bool musical = false;
        double musicalStart = 0.0;
    };
    struct Slot {
        std::atomic<int> state { 0 };                    // 0 free, 1 ready (to the audio thread), 2 done (back)
        uint32_t order = 0;
        int lane = 0;
        ruidocore::Part part;
        ChordLane chords {};
        bool hasChords = false;
    };
    static constexpr int kSlots = 32;
    static constexpr int kMaxPending = 512;

    void takeMail();
    void schedule(double at, bool on, int note, int velocity, bool musical = false, double musicalStart = 0.0);
    void flushAll(Event* out, int maxOut, int& count);
    void fireStep(const ruidocore::FiredStep& f, double stepStart, double stepSamples);

    double sr = 48000.0;
    std::unique_ptr<ruidocore::Sequencer> sequencer;
    std::vector<ruidocore::FiredStep> fired;
    std::array<std::unique_ptr<Slot>, kSlots> slots;
    uint32_t nextOrder = 1;
    std::unique_ptr<std::array<Pending, kMaxPending>> pending;
    int numPending = 0;
    std::array<float, kModLanes> lanes {};
    std::unique_ptr<std::array<ChordLane, kLanes>> chords;
    std::array<std::atomic<float>, kModLanes> lanesOut {};
    std::array<std::atomic<int>, kLanes> cursors {};
    std::array<std::atomic<bool>, kLanes> mutes {};
    std::atomic<int> stepsPerBeat { 4 };
    std::atomic<float> swing { 0.0f };
    std::mt19937 rng { 0x5eedu };

    bool wasPlaying = false;
    double blockStart = 0.0;                             // samples since the transport started
    double nextBoundary = 0.0;                           // the next step's start (samples, same origin)
    double expectedPpq = -1.0;                           // where the next block should begin, from this one's ppq
    double previousBpm = 120.0;
    int64_t globalStep = 0;
};

} // namespace usina::seq
