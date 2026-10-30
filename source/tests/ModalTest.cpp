// SPDX-License-Identifier: GPL-3.0-only
// Validate resonator partial ratios, material decay, strike-position weighting,
// post-excitation ringing and bounded resonant drive.
// Expected T60 = ln(1000)/(pi*f*eta(f)).
#include "engines/fm/FmEngine.h"
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace usina::fm;
constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;

int failures = 0;
void check(bool ok, const char* what, double got, const char* want)
{
    std::printf("%-46s %s  %10.3f  (want %s)\n", what, ok ? "ok  " : "FAIL", got, want);
    failures += ok ? 0 : 1;
}

// OP1 MODAL alone (ALGO 1: OP2 -> OP1, OP2 silent unless asked), struck at A3 (220 Hz), velocity 127
std::map<std::string, int> object(int structure, int material)
{
    return { { "ALGO", 0 }, { "OP1.MODE", MODAL }, { "OP1.A", structure }, { "OP1.B", 0 }, { "OP1.C", 100 },
             { "OP1.D", 50 }, { "OP1.LEVEL", 90 }, { "OP1.SUS", 100 }, { "OP1.REL", 60 }, { "OP1.MATERIAL", material },
             { "OP1.IRREG", 0 }, { "OP1.STRIKE", 100 }, { "OP1.HARDNESS", 100 }, { "OP1.POSITION", 30 } };
}

std::vector<float> render(const std::map<std::string, int>& vals, double secs, double offAt = -1.0,
                          int* voicesAtEnd = nullptr)
{
    auto e = std::make_unique<Engine>();
    e->prepare(kFs, false, kFs);
    e->setPatch(Patch::fromValues(vals));
    e->noteOn(57, 127);
    const int n = (int)(secs * kFs), blk = 256;
    std::vector<float> L((size_t)n, 0.0f), R((size_t)n, 0.0f);
    for (int i = 0; i < n; i += blk) {
        if (offAt >= 0.0 && i <= (int)(offAt * kFs) && i + blk > (int)(offAt * kFs)) e->noteOff(57);
        e->render(L.data() + i, R.data() + i, std::min(blk, n - i));
    }
    if (voicesAtEnd) *voicesAtEnd = e->activeVoices();
    return L;
}

// The power at f over x[a, a + len) (Goertzel, Hann-windowed)
double powerAt(const std::vector<float>& x, size_t a, size_t len, double f)
{
    const double w = 2.0 * kPi * f / kFs, c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = 0; i < len; ++i) {
        const double h = 0.5 - 0.5 * std::cos(2.0 * kPi * (double)i / (double)len);
        const double s = x[a + i] * h + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

// The strongest frequency within +-4 % of f (0.02 % steps)
double peakNear(const std::vector<float>& x, size_t a, size_t len, double f)
{
    double best = f, bp = -1.0;
    for (double r = 0.96; r <= 1.04; r += 0.0002) {
        const double p = powerAt(x, a, len, f * r);
        if (p > bp) { bp = p; best = f * r; }
    }
    return best;
}
double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
} // Namespace

int main()
{
    const auto& tb = modalTable();
    std::printf("      (membrane ratios:");
    for (int i = 0; i < 10; ++i) std::printf(" %.3f", tb.ratio[MS_MEMBRANE][(size_t)i]);
    std::printf("; the literature: 1 1.594 2.136 2.296 2.653 2.918 3.156 3.501 3.600 3.652)\n");
    // --- STRUCTURES: the first partials where the physics puts them (ALUMINIUM: long, nearly perfect) ---
    for (const int st : { MS_STRING, MS_TUBE_CLOSED, MS_BAR, MS_TINE, MS_MEMBRANE, MS_PLATE, MS_BELL, MS_BOWL }) {
        auto v = object(st, MM_ALUMINIUM);
        v["OP1.STIFF"] = -8;                              // Cancels aluminium's own small stiffness: 0.08
        v["OP1.POSITION"] = 37;                           // An irrational-ish point: every mode struck
        const auto x = render(v, 1.2);
        const double f1 = peakNear(x, 4800, 32768, 220.0);
        double worst = 0.0;
        int count = 0;
        for (int i = 1; i < 6; ++i) {
            const double want = 220.0 * tb.ratio[(size_t)st][(size_t)i];
            if (want > 9000.0) break;
            const double got = peakNear(x, 4800, 32768, want) / f1 * 220.0;
            worst = std::max(worst, std::fabs(cents(got, want)));
            ++count;
        }
        const std::string what = std::string(modalStructureName(st)) + ": partials 2.." + std::to_string(count + 1) +
                                 " off the table (cents)";
        check(std::fabs(cents(f1, 220.0)) < 6.0 && worst < 6.0, what.c_str(), worst, "< 6");
    }

    // --- MATERIAL: HARDWOOD's first mode at 220 Hz decays as the law says ---
    {
        const Material& m = modalMaterial(MM_HARDWOOD);
        const double eta = m.eta * std::pow(220.0 / 1000.0, m.gamma), t60 = std::log(1000.0) / (kPi * 220.0 * eta);
        const auto rate = [&](int decay) {                // DB a second, from two 100 ms windows 0.4 s apart
            auto v = object(MS_STRING, MM_HARDWOOD);
            v["OP1.D"] = decay;
            const auto x = render(v, 1.5);
            const double a = 10.0 * std::log10(powerAt(x, (size_t)(0.3 * kFs), 4800, 220.0));
            const double b = 10.0 * std::log10(powerAt(x, (size_t)(0.7 * kFs), 4800, 220.0));
            return (a - b) / 0.4;
        };
        const double r50 = rate(50), r60 = rate(60);
        std::printf("      (HARDWOOD at 220 Hz: T60 %.2f s by the law, %.1f dB/s)\n", t60, 60.0 / t60);
        check(std::fabs(r50 / (60.0 / t60) - 1.0) < 0.10, "HARDWOOD 220 Hz: decay rate / the law's", r50 / (60.0 / t60), "1.00");
        check(std::fabs(r50 / r60 - 2.0) < 0.2, "DECAY 60 (x2): the rate halves (ratio)", r50 / r60, "2.0");
    }

    // --- POSITION: a string struck in its middle loses its even modes ---
    {
        auto v = object(MS_STRING, MM_STEEL);
        v["OP1.POSITION"] = 50;
        auto x = render(v, 0.8);
        const double mid = 10.0 * std::log10(powerAt(x, 4800, 16384, 440.0) / powerAt(x, 4800, 16384, 220.0));
        v["OP1.POSITION"] = 25;
        x = render(v, 0.8);
        const double quarter = 10.0 * std::log10(powerAt(x, 4800, 16384, 440.0) / powerAt(x, 4800, 16384, 220.0));
        check(mid < -30.0 && quarter > -12.0, "STRING struck at 1/2: 2nd mode vs 1st (dB)", mid, "< -30 (at 1/4: > -12)");
    }

    // --- RING: BRONZE rings on after its envelope (ATK 0, DEC 0, SUS 0: done at once); RUBBER's voice ends ---
    {
        auto v = object(MS_BELL, MM_BRONZE);
        v["OP1.SUS"] = 0;
        v["OP1.DEC"] = 0;
        v["OP1.REL"] = 0;
        int voices = 0;
        const auto x = render(v, 2.0, 0.05, &voices);
        const auto rms = [&](double a, double b) {
            double s = 0.0;
            for (size_t i = (size_t)(a * kFs); i < (size_t)(b * kFs); ++i) s += (double)x[i] * x[i];
            return 10.0 * std::log10(s / ((b - a) * kFs) + 1e-20);
        };
        // Its hum's T60 is over a minute: from 0.2 s to 1.8 s it loses a few dB at most
        const double drop = rms(0.2, 0.4) - rms(1.8, 2.0);
        check(drop < 6.0 && voices == 1, "BRONZE bell, envelope done at once: 0.2 -> 1.8 s (dB)", drop, "< 6, voice on");
        auto r = object(MS_MEMBRANE, MM_RUBBER);
        r["OP1.SUS"] = 0;
        r["OP1.DEC"] = 0;
        r["OP1.REL"] = 0;
        render(r, 3.0, 0.05, &voices);
        check(voices == 0, "RUBBER membrane: its voice ends by itself (voices)", voices, "0");
    }

    // --- STABLE: its own oscillator at the first mode's pitch, OSC MIX 100, held 4 s ---
    {
        auto v = object(MS_STRING, MM_ALUMINIUM);
        v["OP1.B"] = 100;
        v["OP1.D"] = 100;                                 // The longest decay: the highest Q
        const auto x = render(v, 4.0);
        bool finite = true;
        float peak = 0.0f;
        for (float s : x) { finite = finite && std::isfinite(s); peak = std::max(peak, std::fabs(s)); }
        check(finite && peak < 2.5f, "driven at resonance for 4 s: bounded (peak)", peak, "< 2.5, finite");
    }
    // --- stage 2: the exciters, TENSION, RATTLE ---
    const auto rmsDb = [](const std::vector<float>& x, double a, double b) {
        double s = 0.0;
        for (size_t i = (size_t)(a * kFs); i < (size_t)(b * kFs); ++i) s += (double)x[i] * x[i];
        return 10.0 * std::log10(s / ((b - a) * kFs) + 1e-20);
    };
    {
        // BOW: a steel string, no mallet, the envelope held: it sustains (a struck one decays)
        auto v = object(MS_STRING, MM_HARDWOOD);
        v["OP1.STRIKE"] = 0;
        v["OP1.EXCITER"] = MX_BOW;
        const auto x = render(v, 2.0);
        const double a = rmsDb(x, 1.0, 1.2), b = rmsDb(x, 1.8, 2.0);
        std::printf("      (bowed hardwood string: %.1f dBFS at 1.0 s, %.1f at 1.8 s)\n", a, b);
        check(a > -50.0 && std::fabs(a - b) < 3.0, "BOW: held, it sustains (dB change 1.0 -> 1.8 s)", a - b, "|x| < 3, heard");
        const double f = peakNear(x, (size_t)(1.0 * kFs), 32768, 220.0);
        check(std::fabs(cents(f, 220.0)) < 15.0, "BOW: it speaks at its first mode (cents)", cents(f, 220.0), "|x| < 15");
        // After note-off, bowed excitation ends and the resonator tail releases its voice.
        auto g = object(MS_BOWL, MM_GLASS);
        g["OP1.STRIKE"] = 0;
        g["OP1.EXCITER"] = MX_BOW;
        g["OP1.D"] = 20;
        g["OP1.REL"] = 10;
        int voices = -1;
        const auto r = render(g, 9.0, 1.5, &voices);
        std::printf("      (bowed glass bowl, let go at 1.5 s: %.1f dBFS at 1.0 s, %.1f at 2.5 s, %.1f at 4.5 s)\n",
                    rmsDb(r, 0.8, 1.2), rmsDb(r, 2.5, 3.0), rmsDb(r, 4.5, 5.0));
        const double fall = rmsDb(r, 2.5, 3.0) - rmsDb(r, 4.5, 5.0);
        check(fall > 20.0, "BOW let go: it rings out (dB fallen 2.5 -> 4.5 s)", fall, "> 20");
        check(voices == 0, "BOW let go: the voice ends (voices at 9 s)", voices, "0");
    }
    {
        // BLOW: a glass bowl breathed into: tonal (its first mode well over the noise around it)
        auto v = object(MS_BOWL, MM_GLASS);
        v["OP1.STRIKE"] = 0;
        v["OP1.EXCITER"] = MX_BLOW;
        const auto x = render(v, 1.5);
        const double tone = powerAt(x, (size_t)(0.5 * kFs), 32768, 220.0);
        const double side = 0.5 * (powerAt(x, (size_t)(0.5 * kFs), 32768, 180.0) + powerAt(x, (size_t)(0.5 * kFs), 32768, 260.0));
        check(10.0 * std::log10(tone / side) > 15.0, "BLOW: a glass bowl sings (tone over the noise, dB)",
              10.0 * std::log10(tone / side), "> 15");
        auto s = object(MS_PLATE, MM_STEEL);
        s["OP1.STRIKE"] = 0;
        s["OP1.EXCITER"] = MX_SCRAPE;
        const auto y = render(s, 1.0);
        check(rmsDb(y, 0.5, 1.0) > -60.0, "SCRAPE: a steel plate dragged sounds on (dBFS RMS)", rmsDb(y, 0.5, 1.0), "> -60");
    }
    {
        // TENSION: a skin struck hard starts sharp and glides down as it decays
        auto v = object(MS_MEMBRANE, MM_SKIN);
        v["OP1.TENSION"] = 100;
        v["OP1.D"] = 70;
        const auto x = render(v, 0.6);
        const double early = peakNear(x, (size_t)(0.01 * kFs), 4096, 220.0), late = peakNear(x, (size_t)(0.35 * kFs), 8192, 220.0);
        check(cents(early, late) > 15.0, "TENSION: struck, the pitch glides down (cents)", cents(early, late), "> 15");
    }
    {
        // RATTLE: the highs (over 5 kHz) of a loud strike, with and without the buzz
        const auto highs = [&](int rattle) {
            auto v = object(MS_MEMBRANE, MM_SKIN);
            v["OP1.RATTLE"] = rattle;
            const auto x = render(v, 0.4);
            double e = 0.0;
            for (double f = 5000.0; f < 15000.0; f += 500.0) e += powerAt(x, 200, 8192, f);
            return 10.0 * std::log10(e + 1e-30);
        };
        const double d = highs(100) - highs(0);
        check(d > 6.0, "RATTLE 100: the highs of a strike (dB more)", d, "> 6");
    }
    {
        // DAMP preserves held output; after release at 0.3 s it attenuates the ringing tail.
        auto v = object(MS_BAR, MM_STEEL);
        const auto held0 = render(v, 0.6);
        v["OP1.DAMP"] = 100;
        const auto held100 = render(v, 0.6);
        double diff = 0.0;
        for (size_t i = 0; i < held0.size(); ++i) diff = std::max(diff, (double)std::fabs(held0[i] - held100[i]));
        check(diff == 0.0, "DAMP 100, key held: as without it (max diff)", diff, "0");
        const auto damped = render(v, 0.6, 0.3);
        v["OP1.DAMP"] = 0;
        const auto ringing = render(v, 0.6, 0.3);
        const double d = rmsDb(ringing, 0.45, 0.55) - rmsDb(damped, 0.45, 0.55);
        check(d > 40.0, "DAMP 100, let go: 0.15 s later (dB under the ring)", d, "> 40");
        v["OP1.DAMP"] = 30;
        const double light = rmsDb(ringing, 0.45, 0.55) - rmsDb(render(v, 0.6, 0.3), 0.45, 0.55);
        check(light > 3.0 && light < 25.0, "DAMP 30: a light touch (dB under the ring)", light, "3 .. 25");
    }
    {
        // DROP impacts become closer and quieter according to restitution.
        auto v = object(MS_BAR, MM_HARDWOOD);
        v["OP1.EXCITER"] = MX_DROP;
        v["OP1.D"] = 15;                                 // A short ring: each impact heard on its own
        const auto x = render(v, 1.5);
        std::vector<double> onsets;                      // 1 ms windows of the signal's slope: a jump of 12 dB
        const int w = (int)(0.001 * kFs);
        double prev = 1e-20;
        for (int i = w; i + w < (int)x.size(); i += w) {
            double e = 0.0;
            for (int j = i; j < i + w; ++j) e += (double)(x[(size_t)j] - x[(size_t)j - 1]) * (x[(size_t)j] - x[(size_t)j - 1]);
            if (e > 16.0 * prev && e > 1e-9 && (onsets.empty() || i / kFs - onsets.back() > 0.03)) onsets.push_back(i / kFs);
            prev = std::max(e, 1e-20);
        }
        bool shrinking = onsets.size() >= 4;
        for (size_t k = 2; k < onsets.size() && k < 5; ++k)
            shrinking = shrinking && onsets[k] - onsets[k - 1] < onsets[k - 1] - onsets[k - 2];
        std::printf("      (DROP onsets:");
        for (size_t k = 0; k < onsets.size() && k < 8; ++k) std::printf(" %.3f", onsets[k]);
        std::printf(" s)\n");
        check(shrinking, "DROP: 4+ impacts, the gaps shrinking (impacts)", (double)onsets.size(), ">= 4, shrinking");
    }
    {
        // The bank at 2 x the host's when oversampled (8 x: a step every 4 samples): the same object as at 1 x - its
        // modes' levels at 220 Hz, 4.4 kHz, 11 kHz within 0.5 dB (the averaging's and the hold's droop corrected)
        const auto at = [&](int os, double f) {
            auto e = std::make_unique<Engine>();
            const double fs = kFs * os;
            e->prepare(fs, false, kFs);
            auto v = object(MS_STRING, MM_ALUMINIUM);
            v["OP1.DENSITY"] = 64;
            v["OP1.POSITION"] = 37;
            e->setPatch(Patch::fromValues(v));
            e->noteOn(57, 127);
            const int n = (int)(0.6 * fs);
            std::vector<float> L((size_t)n), R((size_t)n);
            e->render(L.data(), R.data(), n);
            const double w = 2.0 * kPi * f / fs, c = 2.0 * std::cos(w);   // Goertzel at the engine's own rate
            double s1 = 0.0, s2 = 0.0;
            const size_t a = (size_t)(0.2 * fs), len = (size_t)(0.3 * fs);
            for (size_t i = 0; i < len; ++i) {
                const double h = 0.5 - 0.5 * std::cos(2.0 * kPi * (double)i / (double)len);
                const double s = L[a + i] * h + c * s1 - s2;
                s2 = s1;
                s1 = s;
            }
            return 10.0 * std::log10((s1 * s1 + s2 * s2 - c * s1 * s2) / ((double)len * len) + 1e-30);
        };
        // Against 2 x (the bank at 96 kHz too, a step every sample): only the averaging and the hold differ
        double worst = 0.0;
        for (const double f : { 220.0, 4400.0, 11000.0 }) {
            const double d2 = at(8, f) - at(2, f), d1 = at(8, f) - at(1, f);
            std::printf("      (%.0f Hz: 8x - 2x %+.2f dB, 8x - 1x %+.2f dB)\n", f, d2, d1);
            worst = std::max(worst, std::fabs(d2));
        }
        check(worst < 0.5, "8 x (a bank step every 4) against 2 x: modes' levels (dB)", worst, "< 0.5");
    }
    std::printf("%s: %d failures\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
