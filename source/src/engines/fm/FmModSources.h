// SPDX-License-Identifier: GPL-3.0-only
// Usina FM engine - the modulation's own sources (docs/MODULACAO.md), beside the LFOs and the envelopes:
//   FUNC 1..3   a curve drawn by points (up to 64, each segment bent), run per note: LOOP, ONE-SHOT, ENVELOPE (holds
//               at its SUSTAIN point while the key is down), ENVELOPE LOOP (repeats from the SUSTAIN point to the end
//               while the key is down); SPEED (the whole curve's period), POLARITY, SMOOTH
//   RAND 1..3   one for the whole engine, ticking at its SPEED: RANDOM LOOP (a loop of LENGTH steps, each one changed
//               with the chance FLIP as it comes round: 0 repeats, 100 never), SAMPLE & HOLD (a new value a tick),
//               BINARY (high with the chance PROBA; CORREL the chance a step keeps the last); RISE, FALL smoothed
//   COMB 1..3   per voice, from a SOURCE, a MOD and an AMOUNT: the ten types of the reference (sum, diff, multiply,
//               divide, crossfade, lag, threshold, offset, remap, follower) and ours (min, max, the audio input's
//               level); each says what it does as a formula (combFormula) the editor shows
// Plain C++, no JUCE.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace usina::fm {

// --- FUNC ------------------------------------------------------------------------------------------------------------
constexpr int kMaxFuncPoints = 64;                 // (16 at first: the x2 of a 12-point curve did nothing)
struct FuncPoint { float x = 0.0f, y = 0.0f, curve = 0.0f; };   // x 0..1 (time), y 0..1, curve -1..1: its segment's bend
struct FuncShape {
    std::array<FuncPoint, kMaxFuncPoints> p {};
    int n = 2;
};
enum FuncMode { FM_LOOP, FM_ONESHOT, FM_ENVELOPE, FM_ENVLOOP };

// a new function's curve: FUNC 1 a fall (bent: the reference's first), FUNC 2 a rise and fall, FUNC 3 a rise
inline FuncShape defaultFuncShape(int k)
{
    FuncShape s;
    if (k == 0) { s.p[0] = { 0.0f, 1.0f, -0.6f }; s.p[1] = { 1.0f, 0.0f, 0.0f }; s.n = 2; }
    else if (k == 1) { s.p[0] = { 0.0f, 0.0f, 0.0f }; s.p[1] = { 0.5f, 1.0f, 0.0f }; s.p[2] = { 1.0f, 0.0f, 0.0f }; s.n = 3; }
    else { s.p[0] = { 0.0f, 0.0f, 0.3f }; s.p[1] = { 1.0f, 1.0f, 0.0f }; s.n = 2; }
    return s;
}

// a curve made sane: 1..64 points, x 0..1 and never going back, y 0..1, curve -1..1 (a non-finite number: 0; std::clamp
// would pass a NaN through)
inline FuncShape sanitizeFuncShape(FuncShape c)
{
    const auto fin = [](float v) { return std::isfinite(v) ? v : 0.0f; };
    c.n = std::clamp(c.n, 1, kMaxFuncPoints);
    float last = 0.0f;
    for (int i = 0; i < c.n; ++i) {
        auto& p = c.p[(size_t)i];
        p.x = std::clamp(fin(p.x), last, 1.0f);
        p.y = std::clamp(fin(p.y), 0.0f, 1.0f);
        p.curve = std::clamp(fin(p.curve), -1.0f, 1.0f);
        last = p.x;
    }
    return c;
}

// a segment's bend: u 0..1 -> 0..1, c > 0 slow then fast, c < 0 fast then slow, 0 straight
inline double bend(double u, double c)
{
    if (c == 0.0) return u;
    const double e = std::exp2(std::fabs(c) * 3.0);
    return c > 0.0 ? std::pow(u, e) : 1.0 - std::pow(1.0 - u, e);
}

// the curve at t (0..1)
inline double evalFunc(const FuncShape& s, double t)
{
    if (s.n <= 0) return 0.0;
    if (t <= s.p[0].x || s.n == 1) return s.p[0].y;
    for (int i = 0; i + 1 < s.n; ++i) {
        const auto& a = s.p[(size_t)i];
        const auto& b = s.p[(size_t)i + 1];
        if (t <= b.x) {
            const double w = b.x - a.x, u = w > 1e-9 ? (t - a.x) / w : 1.0;
            return a.y + (b.y - a.y) * bend(u, a.curve);
        }
    }
    return s.p[(size_t)s.n - 1].y;
}

struct FuncState {
    double phase = 0.0, smooth = 0.0;
    bool done = false;
    void start() { phase = 0.0; done = false; }
};

// one control block of a FUNC for a voice: its value (0..1 or -1..1). inc: the curve's share a block; sustainX: the
// SUSTAIN point's x; gate: the key down; smoothK: the smoothing's one-pole coefficient a block (1: none)
inline double funcBlock(FuncState& st, const FuncShape& s, int mode, double inc, double sustainX, bool gate,
                        bool bipolar, double smoothK)
{
    switch (mode) {
    case FM_LOOP:
        st.phase += inc;
        st.phase -= std::floor(st.phase);
        break;
    case FM_ONESHOT:
        st.phase = std::min(1.0, st.phase + inc);
        break;
    case FM_ENVELOPE:
        if (gate && st.phase < sustainX) st.phase = std::min(sustainX, st.phase + inc);
        else if (!gate) st.phase = std::min(1.0, st.phase + inc);
        break;
    default:                                              // ENVELOPE LOOP: from SUSTAIN to the end, again, while held
        st.phase += inc;
        if (gate && st.phase >= 1.0) st.phase = sustainX + (st.phase - 1.0);
        st.phase = std::min(st.phase, 1.0);
        break;
    }
    double y = evalFunc(s, st.phase);
    if (bipolar) y = 2.0 * y - 1.0;
    st.smooth += (y - st.smooth) * smoothK;
    return st.smooth;
}

// --- RAND ------------------------------------------------------------------------------------------------------------
enum RandType { RT_LOOP, RT_SH, RT_BINARY };
struct RandGen {
    double phase = 0.0, value = 0.0, target = 0.0;
    std::array<double, 16> reg {};
    int idx = 0;
    bool bit = false, seeded = false;
    uint32_t rng = 0x2545F491u;
    double uni()                                          // -1..1
    {
        uint32_t x = rng ? rng : 0x2545F491u;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        rng = x;
        return (double)(x >> 8) / 8388608.0 - 1.0;
    }
    double chance() { return 0.5 * (uni() + 1.0); }      // 0..1
    // RETRIG: a step at the next block; a RANDOM LOOP from its first step
    void restart()
    {
        phase = 1.0;
        idx = -1;
    }
    // one control block: inc the ticks' share a block; a, b 0..100 (FLIP / PROBA, LENGTH / CORREL); the smoothing's
    // coefficients a block going up and down (1: a step)
    double block(int type, double inc, int a, int b, double riseK, double fallK)
    {
        if (!seeded) {                                    // (the loop starts full: FLIP 0 repeats random steps, not 0s)
            for (auto& r : reg) r = uni();
            seeded = true;
        }
        phase += inc;
        if (phase >= 1.0) {                               // a tick
            phase -= std::floor(phase);
            if (type == RT_LOOP) {
                const int len = 2 + b * 14 / 100;
                idx = (idx + 1) % len;
                if (chance() < a / 100.0) reg[(size_t)idx] = uni();
                target = reg[(size_t)idx];
            } else if (type == RT_SH) {
                target = uni();
            } else {
                if (!(chance() < b / 100.0)) bit = chance() < a / 100.0;
                target = bit ? 1.0 : -1.0;
            }
        }
        value += (target - value) * (target > value ? riseK : fallK);
        return value;
    }
};

// --- COMB ------------------------------------------------------------------------------------------------------------
enum CombType { CT_SUM, CT_DIFF, CT_MUL, CT_DIV, CT_XFADE, CT_LAG, CT_THRESH, CT_OFFSET, CT_REMAP, CT_FOLLOW, CT_MIN,
                CT_MAX, CT_INPUT, kNumCombTypes };
inline const char* combName(int t)
{
    static const char* const N[kNumCombTypes] = { "SUM", "DIFF", "MULTIPLY", "DIVIDE", "CROSSFADE", "LAG", "THRESHOLD",
                                                  "OFFSET", "REMAP", "FOLLOWER", "MIN", "MAX", "INPUT LEVEL" };
    return t >= 0 && t < kNumCombTypes ? N[t] : "SUM";
}
// what a type does, written out (the editor shows it under the drawing: no manual needed)
inline const char* combFormula(int t)
{
    static const char* const F[kNumCombTypes] = {
        "source + mod x amount", "source - mod x amount", "source x (1 - amount + mod x amount)",
        "source / (1 + |mod x amount| x 3)", "source x (1 - amount) + mod x amount",
        "source, slowed (amount: 0 .. 2 s)", "1 when source > amount, else 0", "source + amount",
        "source bent (amount: < 0 log, > 0 exp)", "source's level, followed (amount: its release)",
        "the lower of source and mod x amount", "the higher of source and mod x amount",
        "the audio input's level (amount: sensitivity)" };
    return t >= 0 && t < kNumCombTypes ? F[t] : "";
}

// one control block: a = the source, b = the mod, amt -1..1; st a voice's state (LAG, FOLLOWER); blockSec a block's
// seconds; input the audio input's level (0..1)
inline double combBlock(int type, double a, double b, double amt, double& st, double blockSec, double input)
{
    double y = 0.0;
    switch (type) {
    case CT_SUM: y = a + b * amt; break;
    case CT_DIFF: y = a - b * amt; break;
    case CT_MUL: y = a * (1.0 - std::fabs(amt) + b * amt); break;
    case CT_DIV: y = a / (1.0 + std::fabs(b * amt) * 3.0); break;
    case CT_XFADE: { const double x = std::fabs(amt); y = a * (1.0 - x) + b * x; break; }
    case CT_LAG: {
        const double t = std::fabs(amt) * 2.0;
        st += (a - st) * (t > 0.0 ? 1.0 - std::exp(-blockSec / t) : 1.0);
        y = st;
        break;
    }
    case CT_THRESH: y = a > amt ? 1.0 : 0.0; break;
    case CT_OFFSET: y = a + amt; break;
    case CT_REMAP: y = (a < 0 ? -1.0 : 1.0) * std::pow(std::fabs(a), std::exp2(amt * 2.0)); break;
    case CT_FOLLOW: {
        const double lvl = std::fabs(a), rel = 0.02 + std::fabs(amt) * 2.0;
        st = lvl > st ? lvl : st + (lvl - st) * (1.0 - std::exp(-blockSec / rel));
        y = st;
        break;
    }
    case CT_MIN: y = std::min(a, b * amt); break;
    case CT_MAX: y = std::max(a, b * amt); break;
    case CT_INPUT: y = input * std::exp2(amt * 4.0); break;
    default: break;
    }
    return std::clamp(y, -1.0, 1.0);
}

} // namespace usina::fm
