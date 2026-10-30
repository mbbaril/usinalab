// SPDX-License-Identifier: GPL-3.0-only
#include "FmTables.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace usina::fm {

namespace {
constexpr double kPi = 3.14159265358979323846;
using Table = std::array<float, kTableN + 1>;

// fn over one cycle (x in 0..1), scaled to peak 1 (gen_opx_tables.py wave())
Table wave(const std::function<double(double)>& fn)
{
    std::vector<double> v(kTableN);
    double peak = 0.0;
    for (int i = 0; i < kTableN; ++i) {
        v[(size_t)i] = fn((double)i / kTableN);
        peak = std::max(peak, std::fabs(v[(size_t)i]));
    }
    if (peak == 0.0) peak = 1.0;
    Table t {};
    for (int i = 0; i < kTableN; ++i) t[(size_t)i] = (float)(v[(size_t)i] / peak);
    t[kTableN] = t[0];
    return t;
}

Table additive(const std::vector<double>& amps)
{
    return wave([&](double x) {
        double s = 0.0;
        for (size_t n = 0; n < amps.size(); ++n) s += amps[n] * std::sin(2 * kPi * (double)(n + 1) * x);
        return s;
    });
}

// a sine with `bits` bits (signed), its steps as the firmware's (quantized(): steps of 2^(16 - bits) in Q15)
Table quantized(int bits)
{
    const double step = (double)(1 << (16 - bits));
    Table t {};
    for (int i = 0; i < kTableN; ++i) {
        const double s = std::round(32767.0 * std::sin(2 * kPi * i / kTableN));
        t[(size_t)i] = (float)(std::clamp(std::round(s / step) * step, -32767.0, 32767.0) / 32767.0);
    }
    t[kTableN] = t[0];
    return t;
}
} // namespace

Tables::Tables()
{
    wave[SIN] = additive({ 1.0 });
    wave[SIN12] = quantized(12);
    wave[SIN8] = quantized(8);
    wave[TRI] = usina::fm::wave([](double x) { return x < 0.25 ? 4 * x : x < 0.75 ? 2 - 4 * x : 4 * x - 4; });
    wave[SAW] = usina::fm::wave([](double x) { return x < 0.5 ? 2 * x : 2 * x - 2; });
    wave[SAWHD] = wave[SAW];
    wave[SQR] = usina::fm::wave([](double x) { return x < 0.5 ? 1.0 : -1.0; });
    wave[SQRHD] = wave[SQR];
    wave[ASAW3] = additive({ 1, 1.0 / 2, 1.0 / 3 });
    wave[ASQR3] = additive({ 1, 0, 1.0 / 3, 0, 1.0 / 5 });
    wave[ATRI3] = additive({ 1, 0, -1.0 / 9, 0, 1.0 / 25 });
    wave[A12345] = additive({ 1, 1, 1, 1, 1 });
    for (int n = 2; n <= 8; ++n) {
        std::vector<double> a((size_t)n, 0.0);
        a[0] = 1.0;
        a[(size_t)n - 1] = 1.0;
        wave[(size_t)(A1P2 + n - 2)] = additive(a);
    }

    // SAW HD, SQR HD: level k keeps the harmonics up to 256 >> k, one scale for all the levels of a wave()
    const auto hd = [](std::array<Table, kHdLevels>& out, const std::function<double(int)>& coef) {
        std::array<std::vector<double>, kHdLevels> lv;
        double peak = 0.0;
        for (int k = 0; k < kHdLevels; ++k) {
            const int h = 256 >> k;
            lv[(size_t)k].assign(kTableN, 0.0);
            for (int i = 0; i < kTableN; ++i) {
                double s = 0.0;
                for (int n = 1; n <= h; ++n) s += coef(n) * std::sin(2 * kPi * n * i / kTableN);
                lv[(size_t)k][(size_t)i] = s;
                peak = std::max(peak, std::fabs(s));
            }
        }
        for (int k = 0; k < kHdLevels; ++k) {
            for (int i = 0; i < kTableN; ++i) out[(size_t)k][(size_t)i] = (float)(lv[(size_t)k][(size_t)i] / peak);
            out[(size_t)k][kTableN] = out[(size_t)k][0];
        }
    };
    hd(hdSaw, [](int n) { return (2 / kPi) * ((n + 1) % 2 ? -1.0 : 1.0) / n; });
    hd(hdSqr, [](int n) { return n % 2 ? (4 / kPi) / n : 0.0; });

    // the chip sines: the exact shape (fn, a cycle in 0..1) sampled finely, its harmonics (DC, sines and cosines) up to
    // 256; level k keeps them up to 256 >> k, the DC always; one scale for all the levels of a wave() (its widest one's
    // peak at 1)
    const auto chip = [](std::array<Table, kHdLevels>& out, const std::function<double(double)>& fn) {
        constexpr int kFine = 16384, kH = 256;
        std::vector<double> x(kFine);
        for (int i = 0; i < kFine; ++i) x[(size_t)i] = fn((i + 0.5) / kFine);
        double dc = 0.0;
        for (const double v : x) dc += v;
        dc /= kFine;
        std::vector<double> a(kH + 1, 0.0), b(kH + 1, 0.0);
        for (int n = 1; n <= kH; ++n) {
            double sa = 0.0, sb = 0.0;
            for (int i = 0; i < kFine; ++i) {
                const double ph = 2 * kPi * n * (i + 0.5) / kFine;
                sa += x[(size_t)i] * std::cos(ph);
                sb += x[(size_t)i] * std::sin(ph);
            }
            a[(size_t)n] = 2.0 * sa / kFine;
            b[(size_t)n] = 2.0 * sb / kFine;
        }
        std::array<std::vector<double>, kHdLevels> lv;
        double peak = 0.0;
        for (int k = 0; k < kHdLevels; ++k) {
            const int h = kH >> k;
            lv[(size_t)k].assign(kTableN, dc);
            for (int i = 0; i < kTableN; ++i) {
                double s = dc;
                for (int n = 1; n <= h; ++n) {
                    const double ph = 2 * kPi * n * i / kTableN;
                    s += a[(size_t)n] * std::cos(ph) + b[(size_t)n] * std::sin(ph);
                }
                lv[(size_t)k][(size_t)i] = s;
                if (k == 0) peak = std::max(peak, std::fabs(s));
            }
        }
        for (int k = 0; k < kHdLevels; ++k) {
            for (int i = 0; i < kTableN; ++i) out[(size_t)k][(size_t)i] = (float)(lv[(size_t)k][(size_t)i] / peak);
            out[(size_t)k][kTableN] = out[(size_t)k][0];
        }
    };
    const auto s2 = [](double x) { return std::sin(2 * kPi * x); };
    chip(hdChip[HALFSIN - HALFSIN], [&](double x) { return x < 0.5 ? s2(x) : 0.0; });
    chip(hdChip[ABSSIN - HALFSIN], [&](double x) { return std::fabs(s2(x)); });
    chip(hdChip[QTRSIN - HALFSIN], [&](double x) { return std::fmod(x, 0.5) < 0.25 ? std::fabs(s2(x)) : 0.0; });
    chip(hdChip[ALTSIN - HALFSIN], [&](double x) { return x < 0.5 ? s2(2 * x) : 0.0; });
    chip(hdChip[CAMELSIN - HALFSIN], [&](double x) { return x < 0.5 ? std::fabs(s2(2 * x)) : 0.0; });
    chip(hdChip[LOGSAW - HALFSIN], [](double x) {
        return x < 0.5 ? std::exp2(-16.0 * (x / 0.5)) : -std::exp2(-16.0 * ((1.0 - x) / 0.5));
    });

    envMs[0] = 0.0;
    for (int v = 1; v < 128; ++v) envMs[(size_t)v] = std::pow(90000.0, (v - 1) / 126.0);

    for (int i = 0; i < kNumCoarse; ++i)        // 0..6: 1/128 .. 1/2; 7..38: 1 .. 32
        coarseLog2[(size_t)i] = i < 7 ? (double)(i - 7) : std::log2((double)(i - 6));

    // the classic 32 (msfa, Apache-2.0: reference algorithm table; the feedback bits are not used), then OPX's 8
    static const uint8_t A[kNumAlgs][6] = {
        { 0xc1, 0x11, 0x11, 0x14, 0x01, 0x14 }, { 0x01, 0x11, 0x11, 0x14, 0xc1, 0x14 }, { 0xc1, 0x11, 0x14, 0x01, 0x11, 0x14 },
        { 0xc1, 0x11, 0x94, 0x01, 0x11, 0x14 }, { 0xc1, 0x14, 0x01, 0x14, 0x01, 0x14 }, { 0xc1, 0x94, 0x01, 0x14, 0x01, 0x14 },
        { 0xc1, 0x11, 0x05, 0x14, 0x01, 0x14 }, { 0x01, 0x11, 0xc5, 0x14, 0x01, 0x14 }, { 0x01, 0x11, 0x05, 0x14, 0xc1, 0x14 },
        { 0x01, 0x05, 0x14, 0xc1, 0x11, 0x14 }, { 0xc1, 0x05, 0x14, 0x01, 0x11, 0x14 }, { 0x01, 0x05, 0x05, 0x14, 0xc1, 0x14 },
        { 0xc1, 0x05, 0x05, 0x14, 0x01, 0x14 }, { 0xc1, 0x05, 0x11, 0x14, 0x01, 0x14 }, { 0x01, 0x05, 0x11, 0x14, 0xc1, 0x14 },
        { 0xc1, 0x11, 0x02, 0x25, 0x05, 0x14 }, { 0x01, 0x11, 0x02, 0x25, 0xc5, 0x14 }, { 0x01, 0x11, 0x11, 0xc5, 0x05, 0x14 },
        { 0xc1, 0x14, 0x14, 0x01, 0x11, 0x14 }, { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x14 }, { 0x01, 0x14, 0x14, 0xc1, 0x14, 0x14 },
        { 0xc1, 0x14, 0x14, 0x14, 0x01, 0x14 }, { 0xc1, 0x14, 0x14, 0x01, 0x14, 0x04 }, { 0xc1, 0x14, 0x14, 0x14, 0x04, 0x04 },
        { 0xc1, 0x14, 0x14, 0x04, 0x04, 0x04 }, { 0xc1, 0x05, 0x14, 0x01, 0x14, 0x04 }, { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x04 },
        { 0x04, 0xc1, 0x11, 0x14, 0x01, 0x14 }, { 0xc1, 0x14, 0x01, 0x14, 0x04, 0x04 }, { 0x04, 0xc1, 0x11, 0x14, 0x04, 0x04 },
        { 0xc1, 0x14, 0x04, 0x04, 0x04, 0x04 }, { 0xc4, 0x04, 0x04, 0x04, 0x04, 0x04 },
        { 0x02, 0x06, 0x01, 0x16, 0x21, 0x14 }, { 0x01, 0x11, 0x11, 0x11, 0x11, 0x14 }, { 0x01, 0x05, 0x14, 0x02, 0x06, 0x24 },
        { 0x01, 0x05, 0x05, 0x05, 0x14, 0x14 }, { 0x02, 0x21, 0x02, 0x25, 0x14, 0x14 }, { 0x02, 0x24, 0x01, 0x05, 0x14, 0x14 },
        { 0x01, 0x05, 0x14, 0x14, 0x14, 0x14 }, { 0x01, 0x12, 0x24, 0x24, 0x24, 0x24 } };
    for (int a = 0; a < kNumAlgs; ++a)
        for (int k = 0; k < 6; ++k) alg[(size_t)a][(size_t)k] = (uint8_t)(A[a][k] & 0x37);   // (bus and add bits only)
}

const Tables& Tables::get()
{
    static const Tables t;
    return t;
}

} // namespace usina::fm
