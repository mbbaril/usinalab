// SPDX-License-Identifier: GPL-3.0-only
// Usina UNISON operator: sums 1..9 detuned copies of the operator waveform.
// A: VOICES (1..9, a list); B: DETUNE (how far the outermost copies go: fine at first, wide at the top); C: MIX (the
// centre copy against the others: 0 the centre alone, 200 the sides on top); D: DRIFT (each copy wandering slowly and
// on its own, as analog oscillators never sit still). Two more of the operator's own values: SUB (a square an octave
// below the centre, its level) and PWM (on the square waves: each copy's pulse width swept by its own slow triangle,
// 0.3..1.3 Hz, as far as PWM says - the strings' shimmer of the old polyphonic synths).
// The centre copy is the operator's own phase: VOICES 1 (or MIX 0) is the plain oscillator. Every copy takes the
// operator's input (phase modulation) the same way, so the stack is played by FM as one oscillator would be. The
// side copies start at random phases each note (the stack's living hiss; in phase, the attack would flam).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace usina::fm {

constexpr int kUnisonMax = 9;

// the outermost copy's offset (a ratio, +-) for DETUNE 0..1: a cubic with a small linear part - the first half of
// the knob a slow chorus, the top a wide smear (at 1: +-9 %, about a semitone and a half)
inline double unisonSpread(double detune)
{
    const double k = std::clamp(detune, 0.0, 1.0);
    return 0.09 * (0.08 * k + 0.92 * k * k * k);
}

// copy i's place in the stack (i 1..n-1; the centre, 0, is 0): alternately above and below, further out as i grows,
// each a little irregular (a fixed hash: the same stack every note, never an even comb that beats in lockstep)
inline double unisonPosition(int i, int n)
{
    if (i <= 0 || n <= 1) return 0.0;
    const int rank = (i + 1) / 2, ranks = n / 2;          // (1..ranks on each side)
    uint32_t h = (uint32_t)i * 2654435761u ^ 0x5bd1e995u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
    const double jitter = 0.85 + 0.15 * ((h >> 8) / 16777216.0);
    return ((i & 1) ? 1.0 : -1.0) * (double)rank / (double)std::max(ranks, 1) * jitter;
}

// MIX 0..1 -> the centre's and each side copy's gains: the centre fades to half as the sides come up
inline void unisonGains(double mix, int n, double& centre, double& side)
{
    const double m = std::clamp(mix, 0.0, 1.0);
    centre = n <= 1 ? 1.0 : 1.0 - 0.5 * m;
    side = n <= 1 ? 0.0 : m;
    // the stack's copies are uncorrelated: held at the centre's own loudness by their power
    const double power = centre * centre + (n - 1) * side * side;
    const double norm = power > 0.0 ? 1.0 / std::sqrt(power) : 1.0;
    centre *= norm;
    side *= norm;
}

// copy i's PWM rate (Hz): 0.3 .. 1.3, the same every note (a fixed hash), never two alike
inline double unisonPwmRate(int i)
{
    uint32_t h = (uint32_t)(i + 1) * 0x9E3779B9u;
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13;
    return 0.3 + 1.0 * ((h >> 8) / 16777216.0);
}

// a triangle 0..1 from a phase in cycles
inline double unisonTri(double ph)
{
    const double f = ph - std::floor(ph);
    return f < 0.5 ? 2.0 * f : 2.0 - 2.0 * f;
}

// one voice's stack: the side copies' phases (cycles), their drifts (ratio offsets) and where the drifts head
struct UnisonState {
    std::array<double, kUnisonMax> ph {}, drift {}, target {}, pwmPh {};
    double subPh = 0.0;
    int countdown = 0;
    // (kept, not worked out again every sample -, the same values: each copy's place for this VOICES, each copy's
    // PWM step for this rate)
    std::array<double, kUnisonMax> pos {}, pwmStep {};
    int posN = 0;
    double pwmFs = 0.0;
    void places(int n, double fs)
    {
        if (n != posN) {
            for (int i = 0; i < kUnisonMax; ++i) pos[(size_t)i] = unisonPosition(i, n);
            posN = n;
        }
        if (fs != pwmFs) {
            for (int i = 0; i < kUnisonMax; ++i) pwmStep[(size_t)i] = unisonPwmRate(i) / fs;
            pwmFs = fs;
        }
    }
    void start(uint32_t& rng)                            // (a note: random phases, the drifts from rest)
    {
        for (int i = 0; i < kUnisonMax; ++i) {
            ph[(size_t)i] = next(rng);
            drift[(size_t)i] = target[(size_t)i] = 0.0;
            pwmPh[(size_t)i] = next(rng);
        }
        subPh = 0.0;
        countdown = 0;
    }
    static double next(uint32_t& r)                      // (0..1, xorshift)
    {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        return (r >> 8) / 16777216.0;
    }
    // the drifts, once a sample: each copy slides toward a target it picks anew every ~40 ms, up to +-0.6 % (about
    // 10 cents) at DRIFT 1; the slide a one-pole of ~80 ms - slow, irregular, never a vibrato
    void drifts(double amount, int n, double fs, uint32_t& rng)
    {
        if (--countdown <= 0) {
            countdown = std::max(1, (int)(0.04 * fs));
            for (int i = 1; i < n; ++i) target[(size_t)i] = (2.0 * next(rng) - 1.0) * 0.006 * amount;
        }
        const double k = 1.0 / (0.08 * fs);
        for (int i = 1; i < n; ++i) drift[(size_t)i] += (target[(size_t)i] - drift[(size_t)i]) * k;
    }
};

} // namespace usina::fm
