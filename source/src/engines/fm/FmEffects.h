// SPDX-License-Identifier: GPL-3.0-only
// Usina FM engine: 17 operator effects. Input is mixed with OSCMIX times the
// oscillator; FREQ offsets the operator pitch in half-semitone units.
// Coefficients use time constants and delay lengths in seconds to preserve
// the reference response at different sample rates (reference: 44117.647 Hz).
//   PEAK EQ   a bell (an SVF's band, k 0.7) at FREQ, GAIN F +-18 dB         SHELV EQ  LOW (E / 2), HIGH (F) +-18 dB
//   PHASER    three TPT all-passes at FREQ, FB F (-0.95 .. +0.95)          RING      x a sine at FREQ, MIX F
//   COMB      y = x + FB y[-period], out y (1 - |FB|)                       DIST, DRIVE  tanh at 1..100 / 1..10, LOW F
//   DEC       held at 2..256 x the pitch, 2..24 bits                        WAVESHAPER  8 shapes (E), DAMP (F)
//   PUNCH     the attack raised up to x2.56, HIGH F                         STRING    Karplus-Strong, DECAY F
//   TUBE      half a period inverted, a soft clip (overblows)               PLATE, BAR  four modes (Chamberlin)
//   SHEET, SPRING  four lines, all-passes, a Householder mix               DISPERSE  1..16 all-passes (a chirp)
// The native lines are the engine's per operator and voice (the firmware's bank of 16 shared lines, and its
// "the bank is full: the input as it is", do not exist here); COMB, STRING and TUBE reach 20 Hz (the firmware: 43 Hz,
// TUBE 22 Hz), SHEET and SPRING keep the firmware's largest network (495 samples at its rate: their sound).
#pragma once
#include "FmFilters.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace usina::fm {

enum Fx { FX_PEQ, FX_SHELV, FX_PHASER, FX_RING, FX_COMB, FX_DIST, FX_DRIVE, FX_DEC, FX_WSHP, FX_PUNCH, FX_STRING,
          FX_TUBE, FX_PLATE, FX_BAR, FX_SHEET, FX_SPRING, FX_DISP, kNumFx };

constexpr double kFwRate = 750000.0 / 17.0;       // the firmware's sample rate (44117.647 Hz)
constexpr int kNumShapes = 8;

// the per-sample settings of an effect, made once a control block (the pitch and the patch as they are then)
struct FxCoef {
    int type = FX_PEQ;
    double c[8] {};                                // the type's own (Effects::setup)
    double g4[4] {}, f4[4] {}, d4[4] {};
    int len[4] {}, n = 0, nm = 0;
    int ni = 0;
    double ak = 0.0;
};

// an operator's effect state: the small one, and its delay line (sized by Effects::lineLength)
struct FxState {
    double z[4] {};
    std::vector<float> line;
    int w = 0;
    double ap = 0.0;
    int dw[4] {};
    double dz[4] {}, ax[4][3] {}, ay[4][3] {};
    double lp[4] {}, bp[4] {};
    double zs[16] {};
    double ax1 = 0.0;                              // ADAA: the distortion's last input
    bool used = false;                             // the line has been written since the last clear

    void clear()
    {
        for (double& v : z) v = 0.0;
        if (used) std::fill(line.begin(), line.end(), 0.0f);
        used = false;
        w = 0;
        ap = 0.0;
        for (int k = 0; k < 4; ++k) {
            dw[k] = 0;
            dz[k] = lp[k] = bp[k] = 0.0;
            for (int j = 0; j < 3; ++j) ax[k][j] = ay[k][j] = 0.0;
        }
        for (double& v : zs) v = 0.0;
        ax1 = 0.0;
    }
};

class Effects {
public:
    // antialias: the distortions (DIST, DRIVE, WAVESHAPER) by their antiderivative (first-order ADAA: the mean of the
    // curve between the last input and this one), as FOLD already is; off = the firmware's exact arithmetic (the A/B)
    void prepare(double sampleRate, bool antialias = false)
    {
        adaa = antialias;
        fs = sampleRate;
        rs = kFwRate / fs;
        combMax = (int)std::ceil(fs / 20.0) + 4;                     // COMB, STRING: 20 Hz; TUBE (half): 10 Hz
        dnRing = std::max(16, (int)std::lround(496.0 * fs / kFwRate));   // SHEET, SPRING: the firmware's 496 at its rate
        lineLen = std::max(combMax, 4 * dnRing) + 4;
        gLow = onePoleG(250.0);
        gHigh = onePoleG(2500.0);
        // the WAVESHAPER shapes (gen_opx_tables.py, the firmware's formulas: 257 points over -1..1)
        const auto cheb = [](int n, double x) { return std::cos(n * std::acos(std::clamp(x, -1.0, 1.0))); };
        for (int s = 0; s < kNumShapes; ++s)
            for (int i = 0; i <= 256; ++i) {
                const double x = -1.0 + 2.0 * i / 256.0;
                double y;
                switch (s) {
                case 0: y = cheb(3, x); break;
                case 1: y = cheb(5, x); break;
                case 2: y = cheb(7, x); break;
                case 3: y = (cheb(3, x) + cheb(5, x) + cheb(7, x)) / 3.0; break;
                case 4: y = std::sin(kPiD * x); break;
                case 5: y = std::sin(2.5 * kPiD * x); break;
                case 6: y = 2.0 * std::fabs(x) - 1.0; break;
                default: y = (std::min(7, (int)((x + 1.0) * 4.0)) - 3.5) / 3.5; break;
                }
                ws[s][i] = std::clamp(y, -1.0, 1.0);
            }
        // their antiderivatives (the table is piecewise linear: its integral, from -1, piecewise quadratic)
        for (int s = 0; s < kNumShapes; ++s) {
            wsI[s][0] = 0.0;
            for (int i = 1; i <= 256; ++i) wsI[s][i] = wsI[s][i - 1] + 0.5 * (ws[s][i - 1] + ws[s][i]) * (2.0 / 256.0);
        }
    }
    int lineLength() const { return lineLen; }

    // the coefficients of effect `type` at E 0..200, F 0..100, its frequency hz (the pitch + FREQ), the operator's
    // own increment opInc (cycles a sample: DEC follows the key, not FREQ)
    void setup(FxCoef& k, int type, double e, double f, double hz, double opInc) const
    {
        k.type = type = ((type % kNumFx) + kNumFx) % kNumFx;
        e = std::clamp(e, 0.0, 200.0);
        f = std::clamp(f, 0.0, 100.0);
        double* c = k.c;
        const double inc = hz / fs;
        switch (type) {
        case FX_PEQ: {
            double s[4];
            svfSet(s, cutoffG(hz, fs, false), 0.7);
            c[0] = s[0]; c[1] = s[1]; c[2] = s[2]; c[3] = 0.7 * db(f);
            break;
        }
        case FX_SHELV: c[0] = db(std::floor(e / 2.0)); c[1] = db(f); break;   // (E / 2: the firmware's e >> 1)
        case FX_PHASER: {
            const double g = cutoffG(hz, fs, false);
            c[0] = g / (1.0 + g);
            c[1] = (f - 50) * 0.019;
            break;
        }
        case FX_DIST:
        case FX_DRIVE:
            c[0] = 1.0 + e * (type == FX_DIST ? 126.0 : 12.0) / 256.0;
            c[1] = db(f);
            break;
        case FX_DEC: {
            c[0] = opInc * std::exp2(1.0 + 7.0 * e / 200.0);         // 2 .. 256 x the pitch
            const int bits = 2 + (int)(f * 22.0 / 100.0);
            c[1] = bits >= 16 ? 0.0 : std::exp2(1.0 - bits);          // the step: +-1.0 in 2^bits levels
            break;
        }
        case FX_PUNCH:
            c[0] = rate(1.0 / 16.0);
            c[1] = rate(1.0 / 1024.0);
            c[2] = e / 128.0;
            c[3] = db(f);
            break;
        case FX_RING: c[0] = inc; c[1] = f / 100.0; break;
        case FX_WSHP:
            c[0] = std::floor(e * kNumShapes / 201.0);
            c[1] = rate((32767.0 - f * 300.0) / 32768.0);
            break;
        case FX_COMB:
        case FX_STRING:
        case FX_TUBE: {
            const bool tube = type == FX_TUBE;
            double d = (tube ? 0.5 : 1.0) / std::max(inc, 1e-9);     // the period (TUBE: half), samples
            if (type == FX_COMB) {
                c[0] = (f - 50) * 0.019;                              // FB -0.95 .. +0.95
                c[1] = 1.0 - std::fabs(c[0]);
            } else {
                c[0] = tube ? -std::exp(-lambda(f) * d) : 0.80 + f * 0.0019960;   // TUBE: a round trip's loss, negated
                d = std::max(d - 0.5, 0.0);                           // less the two-sample mean's half sample
            }
            d = std::clamp(d, 3.0, (double)(combMax - 2));
            const double fl = std::floor(d), fr = d - fl;
            k.ni = (int)fl - 1;                                       // whole samples, then an all-pass of 1 + fr
            k.ak = -fr / (2.0 + fr);
            break;
        }
        case FX_PLATE:
        case FX_BAR: {
            static const double R[2][2][4] = { { { 1.0, 1.730, 2.328, 3.910 }, { 1.0, 1.31531, 1.52577, 1.97739 } },
                                               { { 1.0, 2.756, 5.404, 8.933 }, { 1.0, 1.66012, 2.32468, 2.98877 } } };
            const auto& md = R[type == FX_BAR];
            const double lam = lambda(f);
            k.nm = 0;
            for (int m = 0; m < 4; ++m) {
                const double ph = inc * md[0][m];
                if (ph > 0.2) break;                                  // above FS / 5: left out, and the ones above
                k.f4[m] = 2.0 * std::sin(kPiD * ph);
                k.d4[m] = 2.0 * lam * md[1][m];
                k.nm++;
            }
            break;
        }
        case FX_SHEET:
        case FX_SPRING: {
            static const double C[2][4] = { { 0.80, 0.62, 0.47, 0.36 }, { 0.80, 0.66, 0.55, 0.44 } };
            const bool spring = type == FX_SPRING;
            const double per = 1.0 / std::max(inc, 1e-9), lam = lambda(f);
            const double top = std::floor(per * C[spring][0]);
            for (int m = 0; m < 4; ++m) {
                double l = std::floor(per * C[spring][m]);
                if (top > dnRing - 1) l = std::floor(l * (dnRing - 1) / top);   // a low note: the largest network
                k.len[m] = std::max(8, (int)l);
                k.g4[m] = std::exp(-lam * k.len[m]);
            }
            k.n = spring ? 3 : 2;
            c[0] = spring ? 0.85 : 0.6;                               // the all-passes
            c[1] = rate(spring ? 0.90 : 0.65);                        // the damping
            break;
        }
        case FX_DISP: {
            const double g = cutoffG(hz, fs, false);
            c[0] = g / (1.0 + g);
            k.n = 1 + (int)(f * 15.0 / 100.0);
            break;
        }
        default: break;
        }
    }

    // one sample (x: 1.0 = full) through the effect; the state s its operator's
    double step(FxState& s, const FxCoef& k, double x) const
    {
        const double* c = k.c;
        double* z = s.z;
        switch (k.type) {
        case FX_PEQ: {
            const double v3 = x - z[1], v1 = c[0] * z[0] + c[1] * v3, v2 = z[1] + c[1] * z[0] + c[2] * v3;
            z[0] = hold(2 * v1 - z[0]);
            z[1] = hold(2 * v2 - z[1]);
            return x + c[3] * v1;
        }
        case FX_SHELV: {
            const double y = x + c[0] * lp1(gLow, x, z[0]);
            return y + c[1] * (y - lp1(gHigh, y, z[1]));
        }
        case FX_PHASER: {
            double u = hold(x + c[1] * z[3]);
            u = 2 * lp1(c[0], u, z[0]) - u;
            u = 2 * lp1(c[0], u, z[1]) - u;
            u = 2 * lp1(c[0], u, z[2]) - u;
            z[3] = u;
            return 0.5 * (x + u);
        }
        case FX_DIST:
        case FX_DRIVE: {
            const double u = std::clamp(x * c[0], -64.0, 64.0);
            double y;
            if (adaa) {                                   // tanh's antiderivative: log cosh
                const double du = u - s.ax1;
                y = std::fabs(du) > 1e-5 ? (logCosh(u) - logCosh(s.ax1)) / du : std::tanh(0.5 * (u + s.ax1));
                s.ax1 = u;
            } else {
                y = std::tanh(u);
            }
            return y + c[1] * lp1(gLow, y, z[0]);
        }
        case FX_DEC: {
            double ph = z[1] + c[0];
            if (ph >= 1.0 || c[0] >= 1.0) {
                z[0] = c[1] > 0.0 ? std::floor(x / c[1]) * c[1] : x;
                ph -= std::floor(ph);
            }
            z[1] = ph;
            return z[0];
        }
        case FX_PUNCH: {
            const double ax = std::fabs(x);
            z[0] += (ax - z[0]) * c[0];
            z[1] += (ax - z[1]) * c[1];
            const double d = z[0] > z[1] ? (z[0] - z[1]) / (z[0] + 1e-9) : 0.0;   // the attack's share
            const double y = x * (1.0 + d * c[2]);
            return y + c[3] * (y - lp1(gHigh, y, z[2]));
        }
        case FX_RING: {
            const double r = x * std::sin(2.0 * kPiD * z[0]);
            z[0] += c[0];
            z[0] -= std::floor(z[0]);
            return x + (r - x) * c[1];
        }
        case FX_WSHP: {
            const int sh = (int)c[0];
            const double v = std::clamp(x, -1.0, 1.0);
            double y;
            if (adaa) {
                const double dv = v - s.ax1;
                y = std::fabs(dv) > 1e-6 ? (shapeI(sh, v) - shapeI(sh, s.ax1)) / dv : shape(sh, 0.5 * (v + s.ax1));
                s.ax1 = v;
            } else {
                y = shape(sh, v);
            }
            z[0] += (y - z[0]) * c[1];
            return z[0];
        }
        case FX_COMB:
        case FX_STRING:
        case FX_TUBE: {
            const int L = (int)s.line.size();
            float* d = s.line.data();
            int r = s.w - k.ni;
            if (r < 0) r += L;
            const double a0 = d[r], a1 = d[r == 0 ? L - 1 : r - 1];
            s.ap = k.ak * (a0 - s.ap) + a1;                           // the fraction: y = a (x - y[-1]) + x[-1]
            double y;
            if (k.type == FX_COMB) {
                y = hold(x + c[0] * s.ap);
            } else {
                double v = 0.5 * (s.ap + z[0]);                       // the two-sample mean: the highs die first
                z[0] = s.ap;
                v *= c[0];
                if (k.type == FX_TUBE) {                              // the breath's soft clip: flat at +-1.0
                    v = std::clamp(v, -1.5, 1.5);
                    v -= v * v * v / 6.75;
                }
                y = hold(x + v);
            }
            d[s.w] = (float)std::clamp(y, -2.0, 2.0);                 // (the firmware's line: +-2.0)
            if (++s.w >= L) s.w = 0;
            s.used = true;
            return k.type == FX_COMB ? c[1] * y : y;
        }
        case FX_PLATE:
        case FX_BAR: {
            double y = 0.0;
            for (int m = 0; m < k.nm; ++m) {
                s.lp[m] = hold(s.lp[m] + k.f4[m] * s.bp[m]);
                s.bp[m] = hold(s.bp[m] + k.f4[m] * (x - s.lp[m]) - k.d4[m] * s.bp[m]);
                y += s.bp[m];
            }
            for (int m = k.nm; m < 4; ++m) s.lp[m] = s.bp[m] = 0.0;
            return hold(y);
        }
        case FX_SHEET:
        case FX_SPRING: {
            float* ring[4];
            for (int m = 0; m < 4; ++m) ring[m] = s.line.data() + m * dnRing;
            double sum = 0.0;
            for (int m = 0; m < 4; ++m) {
                int r = s.dw[m] - k.len[m];
                if (r < 0) r += dnRing;
                double v = ring[m][r];
                for (int j = 0; j < k.n; ++j) {                       // y = a (x - y1) + x1
                    const double y = c[0] * (v - s.ay[m][j]) + s.ax[m][j];
                    s.ax[m][j] = v;
                    s.ay[m][j] = y;
                    v = y;
                }
                s.dz[m] += (v - s.dz[m]) * c[1];
                sum += s.dz[m];
            }
            const double half = 0.5 * sum;                            // Householder: z - (2 / 4) sum
            for (int m = 0; m < 4; ++m) {
                const double y = hold(x + k.g4[m] * (s.dz[m] - half));
                ring[m][s.dw[m]] = (float)std::clamp(y, -2.0, 2.0);
                if (++s.dw[m] >= dnRing) s.dw[m] = 0;
            }
            s.used = true;
            return hold(half);
        }
        case FX_DISP: {
            double u = x;
            for (int m = 0; m < k.n; ++m) u = 2 * lp1(c[0], u, s.zs[m]) - u;
            return u;
        }
        default: return x;
        }
    }

private:
    static constexpr double kPiD = 3.14159265358979323846;
    // log cosh x, without overflow: |x| + log(1 + e^-2|x|) - log 2
    static double logCosh(double x) { const double a = std::fabs(x); return a + std::log1p(std::exp(-2.0 * a)) - 0.6931471805599453; }
    // a shape at v (-1..1), linear between its 257 points; and its antiderivative from -1
    double shape(int s, double v) const
    {
        const double pos = (v + 1.0) * 128.0;
        const int j = std::min((int)pos, 255);
        return ws[s][j] + (ws[s][j + 1] - ws[s][j]) * (pos - j);
    }
    double shapeI(int s, double v) const
    {
        const double pos = (v + 1.0) * 128.0, dx = 2.0 / 256.0;
        const int j = std::min((int)pos, 255);
        const double t = pos - j;
        return wsI[s][j] + ws[s][j] * t * dx + (ws[s][j + 1] - ws[s][j]) * t * t * dx * 0.5;
    }
    bool adaa = false;
    double wsI[kNumShapes][257] {};
    // a gain of (v - 50) x 0.36 dB, less one: what a shelf or a bell adds
    static double db(double v) { return std::exp2((v - 50) * 0.0597953) - 1.0; }
    // the decay rate a sample for DECAY F: T60 0.05 .. 8 s (6.91 = ln 1000)
    double lambda(double f) const { return 6.907755 / (fs * 0.05 * std::pow(160.0, f / 100.0)); }
    // a one-pole's coefficient set per sample at the firmware's rate, made for this rate (the same time constant)
    double rate(double c) const { return 1.0 - std::pow(1.0 - c, rs); }
    double onePoleG(double hz) const
    {
        const double g = std::tan(kPiD * hz / fs);
        return g / (1.0 + g);
    }

    double fs = 48000.0, rs = 1.0;
    int combMax = 2404, dnRing = 540, lineLen = 2408;
    double gLow = 0.0, gHigh = 0.0;
    double ws[kNumShapes][257] {};
};

} // namespace usina::fm
