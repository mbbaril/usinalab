// SPDX-License-Identifier: GPL-3.0-only
// Usina - the room the voices play in (2026-10-21, the owner: reverb "to make the objects believable", ported "the
// right way"). The reverb is Emilie Gillet's from her eurorack firmware (MIT), ported line for line in src/fx/port
// (Reverb.h, Limiter.h, FxEngine.h; THIRD_PARTY.md), and played here as that module plays it (its processor.cc):
//
//   pre-limiter (peak 0.9, decay 4 s) -> reverb -> output limiter (peak 0.9, decay 6 s), on blocks of 0.5 ms
//   (the module's 24 samples at 48 kHz); one knob, AMOUNT: mix = a^2 x 0.9, time from a^3, both as the module's.
//   COLOR, the module's own settings at 48 kHz:
//     CLEAN  its digital setting: time 0.3 + 0.6 a^3, damping 0.6;
//     TAPE   its clean-tape setting: time 0.45 + 0.5 a^3, damping 0.4 (darker, longer).
//   (Its two other settings run the whole module at 32 and 24 kHz - the room 1.5 and 2 times longer, the band
//   narrower: they need the reverb run at those rates, a resampler around it - docs/BACKLOG.md.)
//
// The rate: the port's delays are the module's 48 kHz ones times K = 1, 2 or 4 (the host's rate rounded to 48 kHz x
// K), so the room is the same in seconds at 96 and 192 kHz; the damping and the limiters' attacks keep their time
// constants at the host's exact rate. At 44.1 kHz K = 1: the room is 48/44.1 = 1.09 times longer (8.8 %, a slightly
// bigger room), everything else exact.
//
// On the voices (the engine's output, before the input, the loop, the level and the guard): the room is the
// objects'. AMOUNT at 0 and the tail gone (out - in under -120 dBFS for 250 ms), it stops and clears itself: a
// patch without reverb plays bit for bit as before. No allocation in process(); prepare() sizes everything.
#pragma once
#include <memory>

namespace usina::fx {

class RoomReverb {
public:
    enum Color { kClean = 0, kTape, kNumColors };

    RoomReverb();
    ~RoomReverb();

    void prepare(double sampleRate);                     // (allocates: never on the audio thread)
    void reset();                                        // silent and stopped, as prepared
    // in place; wetOnly: the room alone (an AUX return: its mix at the top, AMOUNT its time; stopped, silence)
    void process(float* l, float* r, int n, float amount, int color, bool wetOnly = false);
    bool active() const;
    int rateMultiplier() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};

} // namespace usina::fx
