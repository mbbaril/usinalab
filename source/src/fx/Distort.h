// SPDX-License-Identifier: GPL-3.0-only
// Usina - DISTORT, the distortions (docs/BACKLOG.md, the effects' list; the user, 21/10: "distorções interessantes pra
// noise/industrial"). Ours: modelled from the circuits' principles; the references the user showed (analog folders and
// fuzzes) credited in THIRD_PARTY.md as ideas. One effect, its modes:
//   SATURATE  tanh, CHARACTER adding even harmonics (an asymmetric curve)
//   HARD      a hard clip, CHARACTER lowering its ceiling
//   DIODE     a diode pair: the positive side soft (an exponential knee), the negative harder, CHARACTER the asymmetry
//   FUZZ      a starved fuzz: CHARACTER sags its supply with the level, so it sputters, gates and dies at the notes' ends
//   FOLD      a folder, CHARACTER its corners: from sine folding (soft) to triangle folding (sharp, bright); with
//             SYMMETRY, the asymmetric, metallic kind. (Stages in series would only add gain - DRIVE does that)
//   RECTIFY   CHARACTER from half-wave to full-wave (the octave above), then a soft clip
//   CRUSH     the rate and the bits reduced together by CHARACTER - aliasing on purpose (the name says so)
//   BITS      the samples' bits mangled: XOR by a mask CHARACTER chooses
//   COMPAND   telephone and radio companding: mu-law with CHARACTER's bits (8 down to 2)
// DRIVE before, SYMMETRY a bias (the DC it leaves taken out after), TONE a tilt after, MIX with the dry, OUTPUT.
// The knobs come once a block (the host's, the matrix's): each glides to its new value over about 10 ms, sample by
// sample, so a DRIVE moved by an LFO never steps (zipper).
// SATURATE .. RECTIFY run at 4 x the host's rate (the engine's half-band stages, FmVoiceFilter.h HalfBand: flat to
// 0.46 fs, -99 dB from 0.54; the round trip an all-pass, tests/HalfBandTest),
// with first-order antiderivative antialiasing where the curve has a closed antiderivative (SATURATE, HARD, DIODE,
// FOLD's first stage); the dry for MIX goes through the same stages, undistorted, so mixing makes no comb.
// CRUSH, BITS and COMPAND run at the host's rate: their aliasing is the sound. Plain C++; no allocation in process().
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
    // in place; off: the voices pass untouched (bit for bit) and the state is let go
    void process(float* l, float* r, int n, const Settings& s, bool on);
    bool active() const { return wasOn; }               // (it ran since the last reset: its state holds something)
    static const char* modeName(int m);
    static const char* characterName(int m);             // what CHARACTER means in that mode
    // CHARACTER's value as that mode reads it (CRUSH: the rate's divisor and the bits; BITS: the bits mangled;
    // COMPAND: the bits kept; RECTIFY: half to full; the others: a share), for the panel
    static std::string characterText(int m, double k);
    static bool oversampled(int m) { return m >= kSaturate && m <= kRectify; }

    // the oversampling's round trip alone - 1 x up to 4 x and back, nothing shaped: what the oversampled modes do to a
    // signal's phase (an all-pass, not a delay: tests/HalfBandTest). The dry for MIX goes through one, so mixing makes
    // no comb; a bus holding DISTORT sends its own dry through one too (fx/BusMatch.h: a bus MIX of 50 % without it
    // dipped 12 dB at 5 kHz and 20 dB at 18 kHz, measured 22/10)
    struct RoundTrip {
        usina::fm::HalfBand s1, s2;                      // (1x <-> 2x, 2x <-> 4x)
        double process(double x)
        {
            // (each stage takes its samples in time order: the inner downs as statements - see Distort::process)
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
    // the DC blocker the wet passes in every mode (the DC a bias leaves: a one-pole high-pass at 10 Hz). Linear, so
    // the dry passes one too - for MIX and in a bus (fx/BusMatch.h): its phase in the bass, unmatched, left 3.6 % of a
    // bass-heavy voice in a bus MIX of 50 % (measured 22/10)
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
        usina::fm::HalfBand os1, os2;                    // (1x <-> 2x, 2x <-> 4x: the wet's)
        RoundTrip dryTrip;                               // (the dry's own)
        double adaaX = 0.0;                              // the shaper's last input (antiderivative antialiasing)
        double env = 0.0;                                // FUZZ: the level its supply sags with
        bool gated = false;                              // FUZZ: its gate (with hysteresis)
        DcBlock dcWet, dcDry;                            // the DC blockers (the wet's; the dry's, to match it)
        double toneLp = 0.0;                             // TONE's low-pass
        double hold = 0.0, holdPhase = 1.0;              // CRUSH's sample and hold
    };
    double shape(Channel& c, double x, int mode, double k) const;  // (at 4 x; k the CHARACTER)
    double digital(Channel& c, double x, int mode, double k);      // (CRUSH, BITS, COMPAND)
    struct Knobs {                                       // the knobs as they glide (linear gains, the CHARACTER)
        double drive = 1.0, out = 1.0, mix = 1.0, tilt = 0.0, bias = 0.0, k = 0.5;
    };
    Knobs cur;
    bool fresh = true;                                   // (after a reset: the knobs start where they are, no glide)
    double glide = 0.002;
    double fs = 48000.0;
    double dcPole = 0.999, toneK = 0.1, envUp = 0.01, envDown = 0.0005;
    std::array<Channel, 2> ch {};
    bool wasOn = false;
    int lastMode = -1;
};

} // namespace usina::fx
