// SPDX-License-Identifier: GPL-3.0-only
#include "Distort.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace usina::fx {

namespace {
constexpr double kPi = 3.14159265358979323846;

// Log cosh, without its overflow (|x| - log 2 + log(1 + e^-2|x|))
double logCosh(double x)
{
    const double a = std::fabs(x);
    return a - 0.69314718055994531 + std::log1p(std::exp(-2.0 * a));
}

// The triangle folder (period 4: rises 0..1, falls to -1 at 3, back to 0 at 4) and its antiderivative (piecewise
// quadratic, 0 over a whole period)
double triFold(double x)
{
    const double u = x - 4.0 * std::floor(x / 4.0);
    return u < 1.0 ? u : (u < 3.0 ? 2.0 - u : u - 4.0);
}
double triFoldF(double x)
{
    const double u = x - 4.0 * std::floor(x / 4.0);
    return u < 1.0 ? 0.5 * u * u : (u < 3.0 ? 2.0 * u - 0.5 * u * u - 1.0 : 0.5 * u * u - 4.0 * u + 8.0);
}

// First-order antiderivative antialiasing: the mean of f over [x1, x] (F its antiderivative); near x1, f at the midpoint
template <typename Fn, typename AntiFn>
double adaa(double x, double& x1, Fn f, AntiFn F)
{
    const double d = x - x1;
    const double y = std::fabs(d) > 1.0e-6 ? (F(x) - F(x1)) / d : f(0.5 * (x + x1));
    x1 = x;
    return y;
}
} // Namespace

const char* Distort::modeName(int m)
{
    static const char* const n[kNumModes] = { "SATURATE", "HARD", "DIODE", "FUZZ", "FOLD", "RECTIFY", "CRUSH", "BITS", "COMPAND" };
    return m >= 0 && m < kNumModes ? n[m] : "";
}

const char* Distort::characterName(int m)
{
    static const char* const n[kNumModes] = { "EVEN", "ASYM", "ASYM", "STARVE", "EDGE", "FULL", "CRUSH", "MASK", "BITS" };
    return m >= 0 && m < kNumModes ? n[m] : "CHAR";
}

std::string Distort::characterText(int m, double k)
{
    k = std::clamp(k, 0.0, 1.0);
    char b[48];
    switch (m) {                                         // The same formulas as digital() and shape() below
        case kCrush:
            std::snprintf(b, sizeof b, "/%d %.1f BITS", 1 + (int)std::floor(k * k * 63.0), 16.0 - 12.0 * k);
            break;
        case kBits:
            std::snprintf(b, sizeof b, "%d BITS", 1 + (int)std::floor(k * 14.0));
            break;
        case kCompand:
            std::snprintf(b, sizeof b, "%d BITS", 8 - (int)std::floor(k * 6.0));
            break;
        case kRectify:
            if (k < 0.005) std::snprintf(b, sizeof b, "HALF");
            else if (k > 0.995) std::snprintf(b, sizeof b, "FULL");
            else std::snprintf(b, sizeof b, "%d %% FULL", (int)std::lround(k * 100.0));
            break;
        default:
            std::snprintf(b, sizeof b, "%d %%", (int)std::lround(k * 100.0));
    }
    return b;
}

void Distort::prepare(double sampleRate)
{
    fs = sampleRate;
    dcPole = dcPoleFor(fs);                              // The DC a bias leaves: out from 10 Hz
    toneK = 1.0 - std::exp(-2.0 * kPi * 900.0 / fs);     // TONE's tilt: around 900 Hz
    envUp = 1.0 - std::exp(-1.0 / (0.001 * 4.0 * fs));   // FUZZ's level, at 4 x: 1 ms up, 80 ms down
    envDown = 1.0 - std::exp(-1.0 / (0.080 * 4.0 * fs));
    glide = 1.0 - std::exp(-1.0 / (0.010 * fs));       // The knobs' glide: 10 ms
    reset();
}

void Distort::reset()
{
    for (auto& c : ch) c = Channel {};
    wasOn = false;
    lastMode = -1;
    fresh = true;
}

// One sample at 4 x: x already driven and biased
double Distort::shape(Channel& c, double x, int mode, double k) const
{
    switch (mode) {
        case kSaturate: {                                // Tanh + k/2 tanh^2 (even harmonics); F: log cosh + k/2 (x - tanh)
            const double e = 0.5 * k;
            return adaa(x, c.adaaX, [e](double v) { const double t = std::tanh(v); return t + e * t * t; },
                        [e](double v) { return logCosh(v) + e * (v - std::tanh(v)); });
        }
        case kHard: {                                    // A hard clip, its negative side lower by k (asymmetric)
            const double hi = 1.0, lo = 1.0 - 0.85 * k;
            return adaa(x, c.adaaX, [hi, lo](double v) { return std::clamp(v, -lo, hi); },
                        [hi, lo](double v) {
                            if (v > hi) return hi * v - 0.5 * hi * hi;
                            if (v < -lo) return -lo * v - 0.5 * lo * lo;
                            return 0.5 * v * v;
                        });
        }
        case kDiode: {                                   // 1 - e^-x above 0; below, -a (1 - e^(x/a)): a harder, lower knee
            const double a = 1.0 - 0.8 * k;
            return adaa(x, c.adaaX, [a](double v) { return v >= 0.0 ? 1.0 - std::exp(-v) : -a * (1.0 - std::exp(v / a)); },
                        [a](double v) { return v >= 0.0 ? v + std::exp(-v) - 1.0 : -a * v + a * a * (std::exp(v / a) - 1.0); });
        }
        case kFuzz: {                                    // Level-dependent supply sag and hysteretic gate.
            const double ax = std::fabs(x);              //
            c.env += (ax > c.env ? envUp : envDown) * (ax - c.env);
            const double supply = std::max(0.08, 1.0 - 0.85 * k * std::min(1.0, c.env));
            const double shut = 0.04 * k, open = 0.07 * k;
            c.gated = c.gated ? c.env < open : c.env < shut;
            if (c.gated) return 0.0;
            return supply * std::tanh(1.5 * x / supply);
        }
        case kFold: {                                    // Sine folding (soft corners) to triangle folding (sharp): k
            const double soft = 1.0 - k;
            return adaa(x, c.adaaX,
                        [soft](double v) { return soft * std::sin(0.5 * kPi * v) + (1.0 - soft) * triFold(v); },
                        [soft](double v) { return soft * (-2.0 / kPi) * std::cos(0.5 * kPi * v) + (1.0 - soft) * triFoldF(v); });
        }
        case kRectify: {                                 // Half-wave (k 0) to full-wave (k 1), then a soft clip
            const double r = k * std::fabs(x) + (1.0 - k) * std::max(x, 0.0);
            return std::tanh(r);
        }
        default:
            return x;
    }
}

// One sample at the host's rate: CRUSH, BITS, COMPAND (x already driven and biased)
double Distort::digital(Channel& c, double x, int mode, double k)
{
    x = std::clamp(x, -1.0, 1.0);
    switch (mode) {
        case kCrush: {                                   // The rate (/1 .. /64) and the bits (16 .. 3) down together
            const double divide = 1.0 + std::floor(k * k * 63.0);
            const double levels = std::pow(2.0, 15.0 - 12.0 * k);
            c.holdPhase += 1.0;
            if (c.holdPhase >= divide) {
                c.holdPhase -= divide;
                c.hold = std::round(x * levels) / levels;
            }
            return c.hold;
        }
        case kBits: {                                    // The low bits XOR'ed with a pattern: k chooses how many
            const int bits = 1 + (int)std::floor(k * 14.0);
            const int mask = (1 << bits) - 1;
            const int v = (int)std::lround(x * 32767.0);
            return (double)(int16_t)(v ^ (0x5A5A & mask)) / 32768.0;
        }
        case kCompand: {                                 // Mu-law (mu 255) quantised to 8 .. 2 bits, decoded
            const double mu = 255.0, bits = 8.0 - std::floor(k * 6.0);
            const double levels = std::pow(2.0, bits - 1.0) - 1.0;
            const double y = std::copysign(std::log1p(mu * std::fabs(x)) / std::log1p(mu), x);
            const double q = std::round(y * levels) / levels;
            return std::copysign((std::pow(1.0 + mu, std::fabs(q)) - 1.0) / mu, q);
        }
        default:
            return x;
    }
}

void Distort::process(float* l, float* r, int n, const Settings& s, bool on)
{
    if (!on) {
        if (wasOn) reset();                              // Off: untouched, and the state let go
        return;
    }
    wasOn = true;
    if (s.mode != lastMode) {                            // Reset shaper history on mode changes to avoid discontinuities.
        for (auto& c : ch) {                             //
            c.adaaX = 0.0;
            c.env = 0.0;
            c.gated = false;
            c.hold = 0.0;
            c.holdPhase = 1.0;
        }
        lastMode = s.mode;
    }
    Knobs to;                                            // Parameter smoothing targets.
    to.drive = std::pow(10.0, std::clamp((double)s.driveDb, 0.0, 48.0) / 20.0);
    to.out = std::pow(10.0, std::clamp((double)s.outputDb, -24.0, 12.0) / 20.0);
    to.mix = std::clamp((double)s.mix, 0.0, 1.0);
    to.tilt = std::clamp((double)s.tone, -1.0, 1.0);
    to.bias = 0.5 * std::clamp((double)s.symmetry, -1.0, 1.0);
    to.k = std::clamp((double)s.character, 0.0, 1.0);
    if (fresh) {
        cur = to;
        fresh = false;
    }
    const int mode = s.mode;
    const bool over = mode >= kSaturate && mode <= kRectify;
    float* bufs[2] = { l, r };
    for (int i = 0; i < n; ++i) {
        cur.drive += glide * (to.drive - cur.drive);
        cur.out += glide * (to.out - cur.out);
        cur.mix += glide * (to.mix - cur.mix);
        cur.tilt += glide * (to.tilt - cur.tilt);
        cur.bias += glide * (to.bias - cur.bias);
        cur.k += glide * (to.k - cur.k);
        const double drive = cur.drive, bias = cur.bias, k = cur.k, mix = cur.mix, tilt = cur.tilt;
        for (int c = 0; c < 2; ++c) {
            auto& st = ch[(size_t)c];
            float* b = bufs[c];
            const double x = std::isfinite(b[i]) ? (double)b[i] : 0.0;
            double wet, dry;
            if (over) {                                  // 1 x -> 4 x, shaped, back; the dry the same way, unshaped
                double a, bb, s4[4];
                // Evaluate inner downsamplers sequentially. Argument evaluation order is unspecified
                // and can reverse sample order if nested calls are passed as arguments.
                st.os1.up(x, a, bb);
                st.os2.up(a, s4[0], s4[1]);
                st.os2.up(bb, s4[2], s4[3]);
                for (double& v : s4) v = shape(st, v * drive + bias, mode, k);
                const double w0 = st.os2.down(s4[0], s4[1]);
                const double w1 = st.os2.down(s4[2], s4[3]);
                wet = st.os1.down(w0, w1);
                dry = st.dryTrip.process(x);
            } else {
                wet = digital(st, x * drive + bias, mode, k);
                dry = x;
            }
            const double y = st.dcWet.process(wet, dcPole);    // The bias's DC out
            dry = st.dcDry.process(dry, dcPole);               // The dry the same: it matches the wet's phase
            st.toneLp += toneK * (y - st.toneLp);              // TONE: the tilt around its pivot
            const double hp = y - st.toneLp;
            const double toned = y + 0.5 * tilt * (hp - st.toneLp);
            b[i] = (float)((dry * (1.0 - mix) + toned * mix) * cur.out);
        }
    }
}

} // Namespace usina::fx
