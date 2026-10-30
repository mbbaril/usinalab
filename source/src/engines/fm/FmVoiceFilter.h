// SPDX-License-Identifier: GPL-3.0-only
// Usina voice filtering and oversampling.
//   HalfBand     a 2x up / down stage: two chains of first-order all-passes (the firmware's elliptic half-band,
//                Valenzuela & Constantinides' design as HIIR makes it: flat to 0.23, -99 dB from 0.27 of the high rate)
//   Decimator    N x down to 1 x by half-band stages (the engine rendered at N x: everything in the voice oversampled)
//   VoiceFilter  one voice's filter: the SVF, SK, LADDER, OTA, SK HOT kernels (FmFilters.h) and CMOS; at the engine's
//                rate, or (fw2x: the firmware's way, for the A/B at 1x) the saturating ones at 2x inside
//   Cmos         the CMOS filter: an SVF whose summing stage is an inverter, its integrators OTAs, its states against
//                uneven rails, LIFE its instability (after the behaviours in Koper, Holters, Esqueda, Parker, "A
//                Virtual Analog Model of the EDP Wasp VCF", DAFx 2022, CC BY 4.0; ours, not a port)
//   Casc         SK CASC, the part's: an SK high-pass into an SK low-pass, its output fed back through tanh
// The firmware's per-sample constants were set at its 44117.647 Hz (and 2x that); here they are made again for the
// rate the filter runs at (the same time constants), so a sound is the same at any rate and any oversampling.
#pragma once
#include "FmFilters.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace usina::fm {

struct HalfBand {
    double ux[8] {}, uy[8] {}, dx[8] {}, dy[8] {}, odd = 0.0;

    static double coef(int k)
    {
        static const double c[8] = { 43629846.0, 161603652.0, 322935430.0, 494752857.0,
                                     654471750.0, 792962461.0, 911847123.0, 1019778549.0 };
        return c[k] / 1073741824.0;
    }
    static double chain(double v, double* xs, double* ys, int first)
    {
        for (int k = first; k < 8; k += 2) {               // y = c (x - y[-1]) + x[-1]
            const double y = coef(k) * (v - ys[k]) + xs[k];
            xs[k] = v;
            ys[k] = y;
            v = y;
        }
        return v;
    }
    void up(double x, double& a, double& b) { a = chain(x, ux, uy, 0); b = chain(x, ux, uy, 1); }
    double down(double a, double b)
    {
        const double p = chain(a, dx, dy, 0), q = chain(odd, dx, dy, 1);
        odd = b;
        return 0.5 * (p + q);
    }
    void reset() { *this = HalfBand {}; }
};

// N x (a power of two, 1..16) down to 1 x: in holds N samples, the output is one
class Decimator {
public:
    void setFactor(int n)
    {
        stages = 0;
        while ((1 << stages) < n && stages < 4) ++stages;
        reset();
    }
    void reset() { for (auto& h : hb) h.reset(); }
    float process(const float* in)
    {
        double buf[16];
        int len = 1 << stages;
        for (int i = 0; i < len; ++i) buf[i] = in[i];
        for (int s = 0; s < stages; ++s) {
            len >>= 1;
            for (int i = 0; i < len; ++i) buf[i] = hb[(size_t)s].down(buf[2 * i], buf[2 * i + 1]);
        }
        return (float)buf[0];
    }

private:
    int stages = 0;
    std::array<HalfBand, 4> hb {};
};

// tanh as the firmware computes it (opx_tanh: 256 steps over 0..4, linear between, 1.0 past 4): in a loop that
// oscillates on its own (CMOS at full resonance) the table's small errors change the oscillation; the A/B uses it
inline double fwTanh(double x)
{
    static const auto T = [] {
        std::array<double, 257> t {};
        for (int i = 0; i <= 256; ++i) t[(size_t)i] = std::min(65535.0, std::round(65536.0 * std::tanh(4.0 * i / 256.0))) / 65536.0;
        return t;
    }();
    const double a = std::fabs(x) * 64.0;
    if (a >= 256.0) return x < 0 ? -65535.0 / 65536.0 : 65535.0 / 65536.0;
    const int i = (int)a;
    const double y = T[(size_t)i] + (T[(size_t)i + 1] - T[(size_t)i]) * (a - i);
    return x < 0 ? -y : y;
}

class Cmos {
public:
    void prepare(double rate, bool firmwareMath = false)
    {
        fw = firmwareMath;
        fs = rate;
        tickEvery = std::max(1, (int)std::lround(fs / 11029.4));   // the firmware's: every 8 at 2 x 44117.647 Hz
        envK = 1.0 - std::pow(1.0 - 1.0 / 256.0, 88235.294 / fs);
        noiseK = std::sqrt(88235.294 / fs);                         // (the same noise in the band at any rate)
    }
    void reset()
    {
        lp = bp = env = dr = dth = 0.0;
        snap = 0;
        jt = 0;
        jv = reg = 0;
        rs = 0x6D2B79F5u;
        cnt = 0;
        f = 0.0;
    }
    // one sample: x, the cutoff (log2 Hz), RESO 0..100, LIFE 0..100, the output (FOut LP HP BP DIST)
    double step(double x, double lg, double reso, int life, FOut out)
    {
        reso = std::clamp(reso, 0.0, 100.0);
        const double r = reso / 100.0, r4 = r * r * r * r;
        const double k = 2.0 * (1.0 - r) - 0.4 * r4 * r4 * r4;     // 2 (1 - r) - 0.4 r^12: < 0 at the top
        const double L = std::clamp(life, 0, 100) / 100.0, kick = 1.2 * L;
        if (cnt-- <= 0) {                                    // a tick: the cutoff, its drift and sag, the judder
            cnt = tickEvery - 1;
            const double ld = uni(), ldt = uni();
            dr += (ld * L / 2.0 - dr) / 32.0;                // the cutoff's drift (log2)
            dth += (ldt * L * 0.3 - dth) / 64.0;             // the threshold's
            const double ee = std::min(env / 4.0, 1.0), ld2 = ee * L * 0.75;   // the supply's sag
            if (L > 0.0) {
                const uint32_t rj = rng();
                const double L2 = L * L, u = (double)(rj >> 8) / 16777216.0 - 0.5;
                if (--jt <= 0) {                             // the judder: stepped, redrawn at 8 .. 30 Hz
                    jv = (int)(u * L2 * 1228.0);             // (1/4096 octave: +-0.15 octave x LIFE^2)
                    jt = 368 + (int)((rj & 1023u) * 1010u >> 10);
                }
                if ((double)(rng() >> 8) / 16777216.0 < L2 / 8192.0) {   // a jump between regimes
                    reg = (int)(u * L2 * 2868.0);            // +-0.35 octave x LIFE^2
                    dth = u * L2 / 4.0;                      // and a new threshold
                }
                if (!(rj & 0x700u)) reg -= (reg > 0) - (reg < 0);   // settling back
            }
            const double fc = std::exp2(lg + dr - ld2 + (jv + reg) / 4096.0);
            const double theta = std::min(3.14159265358979323846 * fc / fs, 0.5235987756);   // fc <= fs / 6
            f = 2.0 * std::sin(theta);
            a0 = 0.12 + dth;
            a1 = th(a0);
        }
        const double nz = uni() * L / 2.0 * noiseK;
        const double fb = (4.0 / 1.5) * th(bp * 1.5 / 4.0);         // the diodes on the resonance's way back
        const double e = x - lp - k * fb + nz;
        const double hp = 4.0 * (th(e / 4.0 + a0) - a1);            // the inverter: asymmetric, soft
        double cur = 4.0 * th(hp / 4.0);                           // the OTA
        if (bp > 2.72) snap = 1;                                   // near a rail the OTA's current jumps
        else if (bp < -4.08) snap = -1;
        else if (bp < 1.2 && bp > -1.2) snap = 0;                  // (until the state comes back)
        cur += snap * kick;
        bp = rail(bp + f * cur);
        lp = rail(lp + f * 4.0 * th(bp / 4.0));
        env += (std::fabs(bp) - env) * envK;                       // the supply's load
        switch (out) {
        case FOut::HP: return hp;
        case FOut::BP: return bp;
        case FOut::DIST: return 4.0 * (th(2.5 * lp / 4.0 + 0.3) - 0.2913126);
        default: return lp;
        }
    }

private:
    double th(double x) const { return fw ? fwTanh(x) : std::tanh(x); }
    double rail(double v) const { return v >= 0.0 ? 3.2 * th(v / 3.2) : 4.8 * th(v / 4.8); }
    bool fw = false;
    uint32_t rng()
    {
        uint32_t x = rs ? rs : 0x6D2B79F5u;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return rs = x;
    }
    double uni() { return (double)(rng() >> 8) / 16777216.0 - 0.5; }   // +-0.5

    double fs = 88235.294, envK = 1.0 / 256.0, noiseK = 1.0;
    int tickEvery = 8, cnt = 0, snap = 0, jt = 0, jv = 0, reg = 0;
    double lp = 0, bp = 0, env = 0, dr = 0, dth = 0, f = 0, a0 = 0.12, a1 = 0.1194;
    uint32_t rs = 0x6D2B79F5u;
};

// the cutoff's bounds where a filter runs: the firmware's at 1x (SVF) and 2x (its saturating ones), the native's
struct CutoffRange { double lo, hi; };
constexpr CutoffRange kFw1x { 6.489, 18000.0 }, kFw2x { 12.978, 36000.0 }, kNative { 6.489, 23680.0 };

class VoiceFilter {
public:
    // rate: the voice's; fw2x: the firmware's way (SVF at the rate, the saturating ones and CMOS at 2x inside)
    void prepare(double rate, bool firmware2x)
    {
        fs = rate;
        fw2x = firmware2x;
        cmos.prepare(fw2x ? 2.0 * fs : fs, fw2x);
        reset();
    }
    void reset()
    {
        s = FState {};
        os.reset();
        cmos.reset();
        cnt = 0;
    }
    // one sample: x (the operators' sum), lg the cutoff (log2 Hz), the type (0..17, not CASC), RESO, the input drive
    // (x1..x8, the saturating ones), CMOS LIFE
    double process(double x, double lg, int type, double reso, double drive, int life)
    {
        const FType& t = voiceFilterType(type);
        if (t.kind == FKind::SVF) {                        // linear: at the rate, the coefficients every 8
            if (cnt-- <= 0) {
                cnt = 7;
                fset(c, t, std::exp2(lg), reso, fs, fw2x ? kFw1x.lo : kNative.lo, fw2x ? kFw1x.hi : kNative.hi);
            }
            return fstep(s, c, t, x);
        }
        if (drive > 1.0) x *= drive;                       // FLT DRIVE: into the saturating ones
        if (!fw2x) {
            if (t.kind == FKind::CMOS) return cmos.step(x, lg, reso, life, t.out);
            if (cnt-- <= 0) {
                cnt = 7;
                fset(c, t, std::exp2(lg), reso, fs, kNative.lo, kNative.hi);
            }
            return fstep(s, c, t, x);
        }
        double a, b;                                       // the firmware's: 2x inside
        os.up(x, a, b);
        if (t.kind == FKind::CMOS) {
            a = cmos.step(a, lg, reso, life, t.out);
            b = cmos.step(b, lg, reso, life, t.out);
        } else {
            if (cnt-- <= 0) {                              // (every 16 at 2x: every 8 of the rate's)
                cnt = 7;
                fset(c, t, std::exp2(lg), reso, 2.0 * fs, kFw2x.lo, kFw2x.hi);
            }
            a = fstep(s, c, t, a);
            b = fstep(s, c, t, b);
        }
        return os.down(a, b);
    }

private:
    double fs = 44117.647;
    bool fw2x = false;
    FState s;
    FCoef c;
    HalfBand os;
    Cmos cmos;
    int cnt = 0;
};

// SK CASC: an SK high-pass into an SK low-pass, the low-pass's output back into the high-pass through tanh (fb: x
// -2..2); HOT: both at the signal's level. At the rate, or 2x inside (the firmware's)
class Casc {
public:
    void prepare(double rate, bool firmware2x)
    {
        fs = rate;
        fw2x = firmware2x;
        reset();
    }
    void reset()
    {
        hs = ls = FState {};
        last = 0.0;
        os.reset();
        cnt = 0;
    }
    double process(double x, double lgHp, double lgLp, double hpPeak, double reso, double fb, bool hot)
    {
        const FType H { hot ? FKind::SKH : FKind::SK, FOut::HP, false }, L { hot ? FKind::SKH : FKind::SK, FOut::LP, false };
        const auto run = [&](double u, double rate, const CutoffRange& cr) {
            if (cnt-- <= 0) {
                cnt = fw2x ? 15 : 7;
                fset(hc, H, std::exp2(lgHp), hpPeak, rate, cr.lo, cr.hi);
                fset(lc, L, std::exp2(lgLp), reso, rate, cr.lo, cr.hi);
            }
            u += std::tanh(last) * fb;
            u = fstep(hs, hc, H, u);
            return last = fstep(ls, lc, L, u);
        };
        if (!fw2x) return run(x, fs, kNative);
        double a, b;
        os.up(x, a, b);
        a = run(a, 2.0 * fs, kFw2x);
        b = run(b, 2.0 * fs, kFw2x);
        return os.down(a, b);
    }

private:
    double fs = 44117.647, last = 0.0;
    bool fw2x = false;
    FState hs, ls;
    FCoef hc, lc;
    HalfBand os;
    int cnt = 0;
};

} // namespace usina::fm
