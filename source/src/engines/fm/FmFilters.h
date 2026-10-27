// SPDX-License-Identifier: GPL-3.0-only
// Usina FM engine - the filter kernels of the operators (FILTER, FILTER FM) and of the voice: topology-preserving
// (zero-delay feedback), after Zavalishin's "The Art of VA Filter Design", in floating point. The reference is the
// CALDERA firmware's (usina16 opx_core.c), coefficient for coefficient:
//   SVF     Simper's trapezoidal state-variable filter: LP HP BP (unity at the peak) BR; two in a row for 24 dB
//   SK      a Sallen-Key low- and high-pass, the loop's input through tanh (ported there from sst-filters, GPL-3.0-
//           or-later, the Surge Synth Team); HOT: the loop saturating at the signal's level (S tanh(v / S), S = 4)
//   LADDER  four one-poles in a loop solved at once, tanh on its input; the multimode outputs; OTA: driven softer
// The states are held to +-16 (a runaway clips, never grows: the firmware's opx_fhold).
#pragma once
#include <algorithm>
#include <cmath>

namespace usina::fm {

enum class FKind { SVF, SK, LAD, OTA, SKH, CMOS };
enum class FOut { LP, HP, BP, BR, LP12, HP12, HP24, BP12, DIST };
struct FType { FKind kind; FOut out; bool two; };

// the operators' 12 types (OPX_FT_OP): LPF HPF BPF BRF, LADDER LP12/24 HP12/24 BP6/12, SK LPF/HPF
inline const FType& opFilterType(int i)
{
    static const FType T[12] = {
        { FKind::SVF, FOut::LP, false }, { FKind::SVF, FOut::HP, false }, { FKind::SVF, FOut::BP, false },
        { FKind::SVF, FOut::BR, false }, { FKind::LAD, FOut::LP12, false }, { FKind::LAD, FOut::LP, false },
        { FKind::LAD, FOut::HP12, false }, { FKind::LAD, FOut::HP24, false }, { FKind::LAD, FOut::BP, false },
        { FKind::LAD, FOut::BP12, false }, { FKind::SK, FOut::LP, false }, { FKind::SK, FOut::HP, false } };
    return T[((i % 12) + 12) % 12];
}

// the voice's 18 types (OPX_FT_VOICE): LPF 12 24 SK OTA, HPF 12 24 SK, BPF 6 12, BRF 6 12, SK CASC (the part's: an
// SK high-pass into an SK low-pass, FmVoiceFilter.h Casc), SK LPF HOT, SK HPF HOT, CMOS LP HP BP DIST
constexpr int kNumVoiceFilters = 18, kCascType = 11;
inline const FType& voiceFilterType(int i)
{
    static const FType T[kNumVoiceFilters] = {
        { FKind::SVF, FOut::LP, false }, { FKind::SVF, FOut::LP, true }, { FKind::SK, FOut::LP, false },
        { FKind::OTA, FOut::LP, false }, { FKind::SVF, FOut::HP, false }, { FKind::SVF, FOut::HP, true },
        { FKind::SK, FOut::HP, false }, { FKind::SVF, FOut::BP, false }, { FKind::SVF, FOut::BP, true },
        { FKind::SVF, FOut::BR, false }, { FKind::SVF, FOut::BR, true }, { FKind::SK, FOut::LP, true },
        { FKind::SKH, FOut::LP, false }, { FKind::SKH, FOut::HP, false }, { FKind::CMOS, FOut::LP, false },
        { FKind::CMOS, FOut::HP, false }, { FKind::CMOS, FOut::BP, false }, { FKind::CMOS, FOut::DIST, false } };
    return T[((i % kNumVoiceFilters) + kNumVoiceFilters) % kNumVoiceFilters];
}

constexpr double kFHold = 16.0;
inline double hold(double x) { return std::clamp(x, -kFHold, kFHold); }

// the cutoff (Hz) -> g = tan(pi fc / fs), fc in [lo, hi] (the firmware's: 6.489 Hz .. 18 kHz at its rate; its 2x
// filters 12.98 Hz .. 36 kHz; the native at N x: up to the knob's 23.68 kHz) and SK also <= 0.3 fs
inline double cutoffG(double hz, double fs, bool sk, double lo = 6.489, double hi = 18000.0)
{
    double fc = std::clamp(hz, lo, hi);
    if (sk) fc = std::min(fc, 0.3 * fs);
    fc = std::min(fc, 0.49 * fs);
    return std::tan(3.14159265358979323846 * fc / fs);
}

// one trapezoidal one-pole low-pass step (state z), G = g / (1 + g): the output
inline double lp1(double G, double x, double& z)
{
    const double v = (x - z) * G, y = v + z;
    z = hold(y + v);
    return y;
}

struct FCoef { double c[10] {}; };
struct FState { double z[4] {}; };

inline void svfSet(double* c, double g, double k)
{
    const double d = 1.0 + g * (g + k);
    c[0] = 1.0 / d;
    c[1] = c[0] * g;
    c[2] = c[1] * g;
    c[3] = k;
}

// the coefficients of type t at cutoff hz and RESO 0..100 (opx_fset)
inline void fset(FCoef& fc, const FType& t, double hz, double reso, double fs, double lo = 6.489, double hi = 18000.0)
{
    reso = std::clamp(reso, 0.0, 100.0);
    double* c = fc.c;
    if (t.kind == FKind::SVF) {            // k: 1.414 (Butterworth) .. 0.03; two: 0.765 .. 0.03 after a 1.848 stage
        const double g = cutoffG(hz, fs, false, lo, hi);
        svfSet(c, g, t.two ? 0.765367 - reso * 0.00735357 : 1.414214 - reso * 0.0138419);
        if (t.two) svfSet(c + 4, g, 1.847759);
    } else if (t.kind == FKind::SK || t.kind == FKind::SKH) {
        const double g = cutoffG(hz, fs, true, lo, hi), r1 = 1.0 / (1.0 + g), G = g * r1;
        const double k = 0.01 + reso * 0.0209;    // K 0.01 .. 2.1
        c[0] = G;
        if (t.out == FOut::LP) { c[1] = (k - k * G) * r1; c[2] = -r1; }
        else { c[1] = r1; c[2] = -G * r1; }
        c[3] = k;
        c[4] = 1.0 / (1.0 - k * (G - G * G));    // alpha
        c[5] = 1.0 / k;
    } else {                                      // LADDER, OTA
        const double g = cutoffG(hz, fs, false, lo, hi), r1 = 1.0 / (1.0 + g), G = g * r1;
        const double k = reso * 0.042;            // k 0 .. 4.2
        c[0] = G;
        c[1] = G * G;
        c[2] = c[1] * G;
        c[3] = c[2] * G;
        c[4] = k;
        c[5] = 1.0 / (1.0 + k * c[3]);
        c[6] = 1.0 - G;
        c[7] = 1.0 + (t.kind == FKind::OTA ? k : 0.5 * k);   // the passband made up
    }
}

inline double svf1(double* z, const double* c, double x, FOut o)
{
    const double v3 = x - z[1], v1 = c[0] * z[0] + c[1] * v3, v2 = z[1] + c[1] * z[0] + c[2] * v3;
    z[0] = hold(2 * v1 - z[0]);
    z[1] = hold(2 * v2 - z[1]);
    switch (o) {
    case FOut::LP: return v2;
    case FOut::BP: return c[3] * v1;
    case FOut::HP: return x - c[3] * v1 - v2;
    default: return x - c[3] * v1;                // BR
    }
}

// one sample through type t (x: 1.0 = the firmware's Q24 1.0)
inline double fstep(FState& s, const FCoef& fc, const FType& t, double x)
{
    const double* c = fc.c;
    double* z = s.z;
    switch (t.kind) {
    case FKind::SVF:
        if (t.two) x = svf1(z + 2, c + 4, x, t.out);
        return svf1(z, c, x, t.out);
    case FKind::SK:
    case FKind::SKH: {
        const double S = t.kind == FKind::SKH ? 4.0 : 1.0;   // the loop's ceiling
        if (t.out == FOut::LP) {                  // z: LPF1, HPF1, LPF2
            const double y1 = lp1(c[0], x, z[0]);
            const double u = S * std::tanh(c[4] * (y1 + c[1] * z[2] + c[2] * z[1]) / S);
            const double y = lp1(c[0], u, z[2]);
            lp1(c[0], c[3] * y, z[1]);
            return y;
        }
        const double y1 = x - lp1(c[0], x, z[1]);  // z: LPF1, HPF1, HPF2, the pole more
        const double yk = S * std::tanh(c[3] * c[4] * (y1 + c[2] * z[2] + c[1] * z[0]) / S);
        lp1(c[0], yk - lp1(c[0], yk, z[2]), z[0]);
        double y = yk * c[5];
        y -= lp1(c[0], y, z[3]);
        return y;
    }
    default: {                                    // LADDER, OTA
        const double sg = c[2] * z[0] + c[1] * z[1] + c[0] * z[2] + z[3];
        const double y4 = (c[3] * x + c[6] * sg) * c[5];
        double u = x - c[4] * y4;
        u = t.kind == FKind::OTA ? std::tanh(0.75 * u) * 1.328125 : std::tanh(u);
        const double a = lp1(c[0], u, z[0]), b = lp1(c[0], a, z[1]), cc = lp1(c[0], b, z[2]), d = lp1(c[0], cc, z[3]);
        double y;
        switch (t.out) {
        case FOut::LP12: y = b; break;
        case FOut::HP12: y = u - 2 * a + b; break;
        case FOut::HP24: y = u - 4 * a + 6 * b - 4 * cc + d; break;
        case FOut::BP: y = 2 * (a - b); break;
        case FOut::BP12: y = 4 * (b - 2 * cc + d); break;
        default: y = d; break;
        }
        return y * c[7];
    }
    }
}

} // namespace usina::fm
