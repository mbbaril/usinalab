// SPDX-License-Identifier: GPL-3.0-only
// Stereo delay with cubic Hermite interpolation and 1 ms to 4 s delay time.
// Sync divisions use kSyncBeats. Feedback ranges from 0 to 110%, with a soft limiter.
// DIGITAL crossfades delay changes; TAPE slews them with pitch modulation and loss.
// WOW: 0.6 Hz, up to +/-2.5 ms; FLUTTER: 6.5 Hz, up to +/-0.6 ms.
// FREEZE disables input, filters and saturation, with unity feedback.
// Parameters smooth over 10 ms; bypass fades over 20 ms and clears the delay lines.
// No allocation in process().
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
        double bpm = 120.0;                              // The host's tempo (SYNC's)
        float feedback = 0.4f;                           // 0 .. 1.1
        float lowCutHz = 20.0f, highCutHz = 20000.0f;    // In the loop (20 Hz / 20 kHz: off)
        float wow = 0.3f, flutter = 0.2f;                // 0..1 (TAPE)
        float mix = 0.35f;                               // 0..1
        float outputDb = 0.0f;                           // -24 .. 12
        int character = kDigital, routing = kStereo;
        bool freeze = false;
    };

    void prepare(double sampleRate, double maxSeconds = 4.0);
    void reset();
    // Process in place. Bypass fades echoes over 20 ms, then clears the delay lines.
    void process(float* l, float* r, int n, const Settings& s, bool on);
    bool active() const { return running; }
    double delaySamples(const Settings& s) const;        // Requested delay in samples.
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
    // Smoothed parameter state.
    double fb = 0.0, mix = 0.0, out = 1.0, freezeShare = 0.0, lcK = 0.0, hcK = 1.0, wowDepth = 0.0, flutDepth = 0.0;
    double glide = 0.002;
    // Digital crossfade taps and tape delay slew.
    double tapA = 0.0, tapB = 0.0, tapX = 1.0, tapXStep = 0.0, tapeTime = 0.0, tapeGlide = 0.0;
    int lastCharacter = -1;
    // Per-channel feedback filters, tape loss and modulation phases.
    std::array<OnePole, 2> lcLp, hcLp, tapeLp;
    double wowPh = 0.0, flutPh = 0.0, wowInc = 0.0, flutInc = 0.0, tapeLoss = 0.5;
};

} // Namespace usina::fx
