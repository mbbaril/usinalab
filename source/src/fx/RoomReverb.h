// SPDX-License-Identifier: GPL-3.0-only
// Stereo room reverb with rate-scaled delay lengths and damping.
// Derived DSP attribution and license notices are in src/fx/port and THIRD_PARTY.md.
//
// Pre-limiter -> reverb -> output limiter in 0.5 ms blocks. Both peaks are 0.9;
// limiter decay times are 4 s and 6 s. AMOUNT maps mix to 0.9*a^2 and decay to a^3.
// CLEAN: time=0.3+0.6*a^3, damping=0.6. TAPE: time=0.45+0.5*a^3, damping=0.4.
//
// Delay lengths scale by K = 1, 2 or 4 at nominal 48 kHz multiples.
// Damping and limiter time constants use the exact host rate. At 44.1 kHz,
// the K=1 delay network is 48/44.1 times longer than at 48 kHz.
//
// Process voice audio before external input, feedback, master level and output guard.
// With AMOUNT zero, stop and clear after the residual remains below -120 dBFS for 250 ms.
// No allocation in process().
#pragma once
#include <memory>

namespace usina::fx {

class RoomReverb {
public:
    enum Color { kClean = 0, kTape, kNumColors };

    RoomReverb();
    ~RoomReverb();

    void prepare(double sampleRate);                     // Allocates: never on the audio thread
    void reset();                                        // Reset to silence. wetOnly returns the reverb contribution without dry audio;
    // the bus controls return gain, and AMOUNT controls decay.
    void process(float* l, float* r, int n, float amount, int color, bool wetOnly = false);
    bool active() const;
    int rateMultiplier() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};

} // Namespace usina::fx
