// SPDX-License-Identifier: GPL-3.0-only
// Multimode waveshaper with drive, DC bias, tone tilt and dry/wet mixing.
// SATURATE, HARD, DIODE, FUZZ, FOLD and RECTIFY run at 4x host rate.
// Closed-form shapers use first-order antiderivative antialiasing where available.
// CRUSH, BITS and COMPAND run at host rate with intentional aliasing.
// The dry path matches oversampling and DC-blocking phase.
// Parameters smooth over 10 ms. No allocation in process().
#pragma once
#include "engines/fm/FmVoiceFilter.h"
#include <array>
#include <cmath>
#include <string>

namespace usina::fx {

class Distort {
public:
    enum Mode { kSaturate = 0, kHard, kDiode, kFuzz, kFold, kRectify, kCrush, kBits, kCompand, kNumModes };
    struct Settings {
        int mode = kSaturate;
        float driveDb = 12.0f;                           // 0 .. 48 dB
        float character = 0.5f;                          // 0..1, the mode's own
        float symmetry = 0.0f;                           // -1..1
        float tone = 0.0f;                               // -1 (dark) .. 1 (bright)
        float mix = 1.0f;                                // 0..1
        float outputDb = 0.0f;                           // -24 .. 12 dB
    };

    void prepare(double sampleRate);
    void reset();
    // Process in place. Bypass preserves input samples and resets effect state.
    void process(float* l, float* r, int n, const Settings& s, bool on);
    bool active() const { return wasOn; }               // It ran since the last reset: its state holds something
    static const char* modeName(int m);
    static const char* characterName(int m);             // Mode-specific CHARACTER labels and formatted values.
    static std::string characterText(int m, double k);
    static bool oversampled(int m) { return m >= kSaturate && m <= kRectify; }

    // Unshaped 1x-to-4x-to-1x round trip for dry-path phase matching.
    // This is an all-pass response, not a pure delay; see tests/HalfBandTest.
    struct RoundTrip {
        usina::fm::HalfBand s1, s2;                      // 1x <-> 2x, 2x <-> 4x
        double process(double x)
        {
            // Each stage takes its samples in time order: the inner downs as statements - see Distort::process
            double a, b, d[4];
            s1.up(x, a, b);
            s2.up(a, d[0], d[1]);
            s2.up(b, d[2], d[3]);
            const double e0 = s2.down(d[0], d[1]);
            const double e1 = s2.down(d[2], d[3]);
            return s1.down(e0, e1);
        }
        void reset()
        {
            s1.reset();
            s2.reset();
        }
    };
    // One-pole 10 Hz DC blocker. Apply to both paths to preserve phase when mixing.
    static double dcPoleFor(double sampleRate) { return std::exp(-2.0 * 3.14159265358979323846 * 10.0 / sampleRate); }
    struct DcBlock {
        double x1 = 0.0, y1 = 0.0;
        double process(double x, double pole)
        {
            const double y = x - x1 + pole * y1;
            x1 = x;
            y1 = y;
            return y;
        }
        void reset() { x1 = y1 = 0.0; }
    };

private:
    struct Channel {
        usina::fm::HalfBand os1, os2;                    // 1x <-> 2x, 2x <-> 4x: the wet's
        RoundTrip dryTrip;                               // The dry's own
        double adaaX = 0.0;                              // The shaper's last input (antiderivative antialiasing)
        double env = 0.0;                                // FUZZ: the level its supply sags with
        bool gated = false;                              // FUZZ: its gate (with hysteresis)
        DcBlock dcWet, dcDry;                            // The DC blockers (the wet's; the dry's, to match it)
        double toneLp = 0.0;                             // TONE's low-pass
        double hold = 0.0, holdPhase = 1.0;              // CRUSH's sample and hold
    };
    double shape(Channel& c, double x, int mode, double k) const;  // At 4 x; k the CHARACTER
    double digital(Channel& c, double x, int mode, double k);      // CRUSH, BITS, COMPAND
    struct Knobs {                                       // The knobs as they glide (linear gains, the CHARACTER)
        double drive = 1.0, out = 1.0, mix = 1.0, tilt = 0.0, bias = 0.0, k = 0.5;
    };
    Knobs cur;
    bool fresh = true;                                   // After a reset: the knobs start where they are, no glide
    double glide = 0.002;
    double fs = 48000.0;
    double dcPole = 0.999, toneK = 0.1, envUp = 0.01, envDown = 0.0005;
    std::array<Channel, 2> ch {};
    bool wasOn = false;
    int lastMode = -1;
};

} // Namespace usina::fx
