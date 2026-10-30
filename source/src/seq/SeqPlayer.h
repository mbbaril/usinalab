// SPDX-License-Identifier: GPL-3.0-only
// Host-synchronized sequencer with sample-accurate note events and four modulation lanes.
// Apply probability, conditions, ratchets, accents, microtiming and swing at playback.
// Pair every note-on with a note-off, including repeats, transport stops and seeks.
// Validate host PPQ/BPM and cap playback at 64 steps per block.
// Message-thread edits cross a lock-free mailbox; collect() reclaims retired lanes
// on the message thread. No allocation or deallocation on the audio thread.
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
// Append four percussion lanes after existing lanes to preserve saved lane indices.
// Each lane has one base note; per-step velocity controls hit strength.
constexpr int kDrumLanes = 4;
constexpr int kFirstDrumLane = 1 + kModLanes;
constexpr int kLanes = 1 + kModLanes + kDrumLanes;
constexpr int kDrumBaseNotes[kDrumLanes] = { 36, 41, 48, 55 };
inline bool isNoteLane(int lane) { return lane == kNoteLane || (lane >= kFirstDrumLane && lane < kLanes); }

struct Event {
    int sample = 0;                                      // Within the block
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

// A pattern as the sequencer starts with: a note lane and four modulation lanes, 16 steps each, nothing on
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
    // The whole pattern at once (a session loaded, prepare): NOT while process() can run - the plugin calls it with
    // processing suspended
    void setPatternNow(const ruidocore::Pattern& p);
    // One lane's new content while it plays (message thread): false when the mailbox is full (try again later)
    bool submitLane(int lane, const ruidocore::Part& part, const ChordLane* chords = nullptr);
    // The grid and the swing (any thread; taken at the next block)
    void setGlobals(int stepsPerBeat, float swingPercent);
    // Message thread: frees what the audio thread handed back
    void collect();

    // Audio thread: one host block. Returns how many events were written to out (sorted by sample)
    int process(int numSamples, const Transport& t, Event* out, int maxOut);
    float laneValue(int modLane) const;                  // 0..1 (lane 0..3: SEQ 1..4), as of the last block's end
    int cursor(int lane) const;                          // The lane's real cursor (any thread; -1: not playing)
    void setMuted(int lane, bool m);                     // Any thread: a lane silent, its cursor running
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
    double blockStart = 0.0;                             // Samples since the transport started
    double nextBoundary = 0.0;                           // The next step's start (samples, same origin)
    double expectedPpq = -1.0;                           // Where the next block should begin, from this one's ppq
    double previousBpm = 120.0;
    int64_t globalStep = 0;
};

} // Namespace usina::seq
