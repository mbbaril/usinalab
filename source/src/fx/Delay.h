// SPDX-License-Identifier: GPL-3.0-only
// Usina - DELAY (22/10; docs/BACKLOG.md, the effects' list: the one every synth's effects have). Ours:
//   TIME       free (1 ms .. 4 s) or in time with the host (SYNC: 1/32 .. 4 bars, FmSchema.h kSyncBeats)
//   FEEDBACK   0 .. 110 %: past 100 % it grows until the loop's limiter holds it (a smooth knee, no hard clip)
//   LOW CUT, HIGH CUT   in the loop: each echo thinner or darker than the last (at their ends: off)
//   ROUTING    STEREO (each side its own line), PING-PONG (the input, summed, into the left line; each line feeds the
//              other: the echoes bounce), MONO (one line, on both sides)
//   CHARACTER  DIGITAL - clean; a new TIME crossfades from the old echo to the new one (no pitch bend, no zipper).
//              TAPE - a new TIME glides (the pitch bends, as a tape's speed changes), WOW and flutter move the read,
//              the loop saturates softly and loses its highs a little each pass
//   WOW, FLUTTER   TAPE's slow wander of the speed (0.6 Hz, up to +-2.5 ms) and its fast tremble (6.5 Hz, up to
//              +-0.6 ms), each its own knob
//   FREEZE     the loop held as it is: the input shut, the loop's gain exactly 1, its filters and saturation out -
//              it repeats until let go
//   MIX, OUTPUT
// Read between samples with a cubic (Hermite): a moving read (TAPE's glide, WOW) stays clean. No latency. The knobs
// come once a block: each glides sample by sample. Switched on or off, faded (20 ms); off, the input passes untouched
// and the lines are emptied. Plain C++; no allocation in process().
#pragma once
#include <array>
#include <vector>

namespace usina::fx {

class Delay {
public:
    enum Character { kDigital = 0, kTape, kNumCharacters };
    enum Routing { kStereo = 0, kPingPong, kMono, kNumRoutings };
    struct Settings {
        float timeMs = 375.0f;                           // 1 .. 4000 (free)
        int sync = 0;                                    // 0: free; else a division (FmSchema.h kSyncBeats 1..14)
        double bpm = 120.0;                              // the host's tempo (SYNC's)
        float feedback = 0.4f;                           // 0 .. 1.1
        float lowCutHz = 20.0f, highCutHz = 20000.0f;    // in the loop (20 Hz / 20 kHz: off)
        float wow = 0.3f, flutter = 0.2f;                // 0..1 (TAPE)
        float mix = 0.35f;                               // 0..1
        float outputDb = 0.0f;                           // -24 .. 12
        int character = kDigital, routing = kStereo;
        bool freeze = false;
    };

    void prepare(double sampleRate, double maxSeconds = 4.0);
    void reset();
    // in place; off: the input passes untouched (after a 20 ms fade of the echoes) and the lines are emptied
    void process(float* l, float* r, int n, const Settings& s, bool on);
    bool active() const { return running; }
    double delaySamples(const Settings& s) const;        // the TIME the settings ask, in samples (for the panel, tests)
    static const char* characterName(int c);
    static const char* routingName(int r);

private:
    float read(const std::vector<float>& b, double back) const;   // `back` samples behind the write head (cubic)
    struct OnePole {
        double y = 0.0;
        double lp(double x, double k) { return y += k * (x - y); }
    };
    std::array<std::vector<float>, 2> line;
    int mask = 0, w = 0;
    double fs = 48000.0, maxDelay = 4.0 * 48000.0;
    bool running = false, fresh = true;
    float fade = 0.0f, fadeStep = 0.001f;
    // the knobs as they glide
    double fb = 0.0, mix = 0.0, out = 1.0, freezeShare = 0.0, lcK = 0.0, hcK = 1.0, wowDepth = 0.0, flutDepth = 0.0;
    double glide = 0.002;
    // TIME: DIGITAL's two taps and the crossfade between them; TAPE's gliding time
    double tapA = 0.0, tapB = 0.0, tapX = 1.0, tapXStep = 0.0, tapeTime = 0.0, tapeGlide = 0.0;
    int lastCharacter = -1;
    // the loop's filters (a side each), TAPE's own loss, the wow's phases
    std::array<OnePole, 2> lcLp, hcLp, tapeLp;
    double wowPh = 0.0, flutPh = 0.0, wowInc = 0.0, flutInc = 0.0, tapeLoss = 0.5;
};

} // namespace usina::fx
