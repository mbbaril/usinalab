// SPDX-License-Identifier: GPL-3.0-only
// Usina FM engine: chaotic maps and dynamic stochastic oscillators.
// Technical references and attribution: THIRD_PARTY.md.
//
// CHAOS: an operator becomes a chaotic generator. The maps step once a cycle of the operator's phase - the noise has a
// pitch, and the matrix's FM, moving that phase, shakes it:
//   LOGISTIC  y = r y (1 - y)                         CHAOS: r 3.5 .. 4     SHAPE: where it starts
//   HENON     x = 1 - a x^2 + b x[-1]                 CHAOS: a 1 .. 1.4     SHAPE: b 0 .. 0.3
//   LATOO     Latoocarfian: x = sin(b y) + c sin(b x), y = sin(a x) + d sin(a y), c = d = 0.5
//             CHAOS: a 1 .. 3   SHAPE: b 2.5 .. 3
//   GBMAN     the gingerbread man: x = 1 - y + |x|, y = x[-1]                      CHAOS, SHAPE: where it starts
//   STANDARD  the standard map: y += k sin x, x += y (mod 2 pi)                    CHAOS: k 0.5 .. 2  SHAPE: start
//   CRACKLE   y = |p y[-1] - y[-2] - 0.05|            CHAOS: p 1 .. 2       SHAPE: where it starts
//   DUST      random impulses, as many a second as the operator's pitch            CHAOS: from Dust (0..1) to Dust2
//             (-1..1)
// SMOOTH interpolates successive map values. After 32 unchanged steps, a small
// random perturbation moves the map away from a fixed point.
// LORENZ is integrated per sample; SHAPE sets about 1/4..2 attractor loops per
// pitch cycle. CHAOS sets r in [25, 60], above the fixed-point threshold ~24.74.
//
// STOCHASTIC: GENDY, dynamic stochastic synthesis - a cycle of breakpoints whose levels and lengths walk at random
// as the cycle passes them, the steps drawn from one of Xenakis' distributions (each as an inverse of its own
// cumulative distribution, its spread fixed, -1..1): LINEAR, CAUCHY, LOGISTIC, HYPERBOLIC COSINE, ARCSINE,
// EXPONENTIAL. A: the distribution; B: the levels' step; C: how many points (3 .. 32); D: the lengths' step. A cycle
// of points lasts a cycle of the operator's pitch on average; with both steps at 0 it is a fixed wave at that pitch.
// Output -1..1. Plain C++, no allocation.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace usina::fm {

enum ChaosModel { CH_LOGISTIC, CH_HENON, CH_LORENZ, CH_LATOO, CH_GBMAN, CH_STANDARD, CH_CRACKLE, CH_DUST, kChaosModels };
inline const char* chaosModelName(int m)
{
    static const char* const N[kChaosModels] = { "LOGISTIC", "HENON", "LORENZ", "LATOO", "GBMAN", "STANDARD",
                                                 "CRACKLE", "DUST" };
    return N[((m % kChaosModels) + kChaosModels) % kChaosModels];
}
enum StochDist { SD_LINEAR, SD_CAUCHY, SD_LOGISTIC, SD_HYPCOS, SD_ARCSINE, SD_EXPON, kStochDists };
inline const char* stochDistName(int d)
{
    static const char* const N[kStochDists] = { "LINEAR", "CAUCHY", "LOGISTIC", "HYP COSINE", "ARCSINE", "EXPONENTIAL" };
    return N[((d % kStochDists) + kStochDists) % kStochDists];
}

namespace detail {
inline double xorUni(uint32_t& rng)                      // (0..1)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (rng >> 8) / 16777216.0;
}
} // namespace detail

class Chaos {
public:
    // a note's start: the map from its starting point (SHAPE, where a map has no second value)
    void start(int model, double chaos, double shape, uint32_t seed)
    {
        rng = seed ? seed : 0x9E3779B9u;
        const double s = shape / 200.0;                  // (0..1)
        x = y = z = x1 = 0.0;
        switch (model) {
            case CH_LOGISTIC: x = 0.0513 + 0.897 * s; break;   // (never 0.5 exactly: at r 4, 0.5 -> 1 -> 0, held)
            case CH_HENON: x = 0.1; break;
            case CH_LORENZ: x = -6.0; y = -8.0; z = 20.0; break;   // (on the attractor: no slow climb out of 0)
            case CH_LATOO: x = 0.5; y = 0.5; break;
            case CH_GBMAN: x = 1.2 + chaos / 100.0; y = 2.1 + s; break;
            case CH_STANDARD: x = 0.5 + 2.0 * s; break;
            case CH_CRACKLE: x = 0.3 * s; break;
            default: break;
        }
        prev = cur = value(model);
        lastP = -1.0;
        still = 0;
    }

    // one sample: p the operator's phase (cycles, the matrix's FM in it); the model, CHAOS 0..100, SHAPE 0..200,
    // SMOOTH 0..100
    float process(double p, int model, double chaos, double shape, double smooth)
    {
        if (lastP < 0.0) lastP = p;                      // (the first sample: no step yet)
        const double dp = p - lastP;
        lastP = p;
        if (model == CH_DUST) {                          // (a step of the phase: an impulse, its chance the step itself)
            if (uni() >= std::clamp(dp, 0.0, 1.0)) return 0.0f;
            const double u = uni(), mix = chaos / 100.0;
            return (float)(u * (1.0 - mix) + (2.0 * u - 1.0) * mix);
        }
        if (model == CH_LORENZ) {
            flow(std::clamp(dp, 0.0, 0.5), chaos, shape);
            return (float)value(CH_LORENZ);
        }
        const int steps = std::clamp((int)(std::floor(p) - std::floor(p - dp)), 0, 4);   // (forward crossings: steps)
        for (int i = 0; i < steps; ++i) {
            iterate(model, chaos, shape);
            prev = cur;
            cur = value(model);
            still = std::fabs(cur - prev) < 1e-7 ? still + 1 : 0;
            if (still >= 32) kick(model);
        }
        const double fr = p - std::floor(p), sm = std::clamp(smooth / 100.0, 0.0, 1.0);
        const double line = prev + (cur - prev) * fr;
        return (float)std::clamp(cur + (line - cur) * sm, -1.0, 1.0);
    }

private:
    double uni() { return detail::xorUni(rng); }

    void iterate(int model, double chaos, double shape)
    {
        const double c = std::clamp(chaos / 100.0, 0.0, 1.0), s = std::clamp(shape / 200.0, 0.0, 1.0);
        constexpr double kTwoPi = 6.283185307179586;
        switch (model) {
            case CH_LOGISTIC: {
                const double r = 3.5 + 0.5 * c;
                x = r * x * (1.0 - x);
                break;
            }
            case CH_HENON: {
                const double a = 1.0 + 0.4 * c, b = 0.3 * s;
                const double nx = 1.0 - a * x * x + b * x1;
                x1 = x;
                x = nx;
                break;
            }
            case CH_LATOO: {
                const double a = 1.0 + 2.0 * c, b = 2.5 + 0.5 * s;
                const double nx = std::sin(b * y) + 0.5 * std::sin(b * x), ny = std::sin(a * x) + 0.5 * std::sin(a * y);
                x = nx;
                y = ny;
                break;
            }
            case CH_GBMAN: {
                const double nx = 1.0 - y + std::fabs(x);
                y = x;
                x = nx;
                break;
            }
            case CH_STANDARD: {
                const double k = 0.5 + 1.5 * c;
                y = std::fmod(y + k * std::sin(x), kTwoPi);
                if (y < 0.0) y += kTwoPi;
                x = std::fmod(x + y, kTwoPi);
                if (x < 0.0) x += kTwoPi;
                break;
            }
            case CH_CRACKLE: {
                const double pr = 1.0 + c;
                const double nx = std::fabs(pr * x - x1 - 0.05);
                x1 = x;
                x = nx;
                break;
            }
            default: break;
        }
        guard(model);
    }

    // LORENZ: the flow over dp cycles of the phase - SHAPE's loops a cycle (an attractor's loop: about 0.7 of its
    // time), in Euler steps of at most 0.005
    void flow(double dp, double chaos, double shape)
    {
        const double c = std::clamp(chaos / 100.0, 0.0, 1.0), s = std::clamp(shape / 200.0, 0.0, 1.0);
        const double sigma = 10.0, beta = 8.0 / 3.0, r = 25.0 + 35.0 * c;
        const double loops = 0.25 * std::exp2(3.0 * s);  // (1/4 .. 2 loops a cycle)
        const double t = dp * loops * 0.7;
        const int n = std::clamp((int)std::ceil(t / 0.005), 1, 64);
        const double h = t / n;
        for (int i = 0; i < n; ++i) {
            const double dx = sigma * (y - x), dy = x * (r - z) - y, dz = x * y - beta * z;
            x += h * dx;
            y += h * dy;
            z += h * dz;
        }
        guard(CH_LORENZ);
    }

    // (a map run away - non-finite, or out of all reason - or the logistic map on its fixed points 0 and 1: back to a
    // start of its own)
    void guard(int model)
    {
        if ((model == CH_LOGISTIC && (x <= 1e-9 || x >= 1.0)) || !std::isfinite(x) || !std::isfinite(y) ||
            !std::isfinite(z) || std::fabs(x) > 1e6 || std::fabs(y) > 1e6 || std::fabs(z) > 1e6) {
            if (model == CH_LORENZ) {
                x = -6.0;
                y = -8.0;
                z = 20.0;
            } else {
                x = 0.1 + 0.5 * uni();
                y = 0.1;
                z = 0.0;
                x1 = 0.0;
            }
        }
    }

    // a still map knocked off its point (a small step, at random)
    void kick(int model)
    {
        still = 0;
        const double d = 0.05 * (2.0 * uni() - 1.0);
        if (model == CH_LOGISTIC) {
            x = std::clamp(x + d, 0.01, 0.99);
        } else {
            x += d;
            y += 0.05 * (2.0 * uni() - 1.0);
        }
    }

    // the map's value, scaled to -1..1 (each by its own range)
    double value(int model) const
    {
        double v = 0.0;
        switch (model) {
            case CH_LOGISTIC: v = 2.0 * x - 1.0; break;
            case CH_HENON: v = x / 1.5; break;
            case CH_LORENZ: v = x / 25.0; break;
            case CH_LATOO: v = x / 1.5; break;
            case CH_GBMAN: v = (x - 2.5) / 5.5; break;
            case CH_STANDARD: v = x / 3.141592653589793 - 1.0; break;
            case CH_CRACKLE: v = x - 0.5; break;
            default: v = 0.0; break;
        }
        return std::clamp(v, -1.0, 1.0);
    }

    double x = 0.0, y = 0.0, z = 0.0, x1 = 0.0;          // the map's state (x1: its last x, for the 2-step maps)
    double prev = 0.0, cur = 0.0, lastP = -1.0;
    int still = 0;
    uint32_t rng = 0x9E3779B9u;
};

class Stochastic {
public:
    static constexpr int kMaxPoints = 32;

    // a note's start: its points (C: how many), their levels at random, their lengths even
    void start(double pointsValue, uint32_t seed)
    {
        rng = seed ? seed : 0x9E3779B9u;
        points = 3 + (int)std::lround(std::clamp(pointsValue / 200.0, 0.0, 1.0) * (kMaxPoints - 3));
        for (int i = 0; i < points; ++i) {
            amp[(size_t)i] = (float)(2.0 * uni() - 1.0);
            dur[(size_t)i] = 1.0f;
        }
        seg = 0;
        segPos = 0.0;
        lastP = -1.0;
    }

    // one sample: p the operator's phase (the matrix's FM in it); the distribution, the levels' step 0..100, the
    // lengths' step 0..100
    float process(double p, int dist, double ampStep, double durStep)
    {
        if (lastP < 0.0) lastP = p;
        const double dp = std::clamp(p - lastP, 0.0, 0.5);
        lastP = p;
        const double as = std::clamp(ampStep / 100.0, 0.0, 1.0), ds = std::clamp(durStep / 100.0, 0.0, 1.0);
        segPos += dp * points;                           // (in points: a point's length 1 on average)
        for (int guard = 0; guard < 8 && segPos >= dur[(size_t)seg]; ++guard) {
            segPos -= dur[(size_t)seg];
            seg = (seg + 1) % points;
            amp[(size_t)seg] = (float)mirror(amp[(size_t)seg] + 0.3 * as * draw(dist), -1.0, 1.0);
            dur[(size_t)seg] = (float)mirror(dur[(size_t)seg] + 0.3 * ds * draw(dist), 0.25, 1.75);
        }
        const int nxt = (seg + 1) % points;
        const double t = std::clamp(segPos / std::max(1e-6, (double)dur[(size_t)seg]), 0.0, 1.0);
        return (float)(amp[(size_t)seg] + (amp[(size_t)nxt] - amp[(size_t)seg]) * t);
    }

    // one step from the distribution, -1..1: the inverse of its cumulative distribution at a uniform z (its spread
    // fixed; the tails cut at the ends of the range)
    double draw(int dist)
    {
        const double z = std::clamp(uni(), 1e-6, 1.0 - 1e-6), u = 2.0 * z - 1.0;
        constexpr double kPi = 3.141592653589793;
        double v = 0.0;
        switch (dist) {
            case SD_CAUCHY: v = 0.1 * std::tan(kPi * (z - 0.5)); break;                 // (heavy tails)
            case SD_LOGISTIC: v = 0.15 * std::log(z / (1.0 - z)); break;
            case SD_HYPCOS: v = 0.2 * std::log(std::tan(kPi * z / 2.0)); break;         // (the sech distribution)
            case SD_ARCSINE: v = std::sin(kPi * (z - 0.5)); break;                      // (towards the ends)
            case SD_EXPON: v = (z < 0.5 ? 1.0 : -1.0) * 0.2 * std::log(2.0 * std::min(z, 1.0 - z)); break;   // (two-sided)
            default: v = u; break;                                                      // LINEAR
        }
        return std::clamp(v, -1.0, 1.0);
    }

private:
    double uni() { return detail::xorUni(rng); }
    static double mirror(double v, double lo, double hi)
    {
        for (int i = 0; i < 4 && (v < lo || v > hi); ++i) v = v < lo ? 2.0 * lo - v : 2.0 * hi - v;
        return std::clamp(v, lo, hi);
    }

    uint32_t rng = 0x9E3779B9u;
    std::array<float, kMaxPoints> amp {}, dur {};
    int points = 12, seg = 0;
    double segPos = 0.0, lastP = -1.0;
};

} // namespace usina::fm
