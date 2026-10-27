// SPDX-License-Identifier: GPL-3.0-only
// Usina FM engine - the MODAL operator mode: the operator as a physical object (docs/FISICA.md). A bank of up to 64
// resonant modes, each a band-pass state-variable filter (the modal-resonator idea of Emilie Gillet's MIT-licensed
// modules: THIRD_PARTY.md, "ideas"; this bank is written here), its frequencies, decays and loudness set by physics:
//   STRUCTURE  the shape, which fixes the modes' frequency ratios (a string's harmonics, a free bar's 1 : 2.756 :
//              5.404, a membrane's Bessel zeros, a bell's hum-prime-tierce-quint-nominal ...)
//   MATERIAL   how it loses energy: a loss factor eta, each mode decaying in T60 = ln(1000) / (pi f eta(f)), with
//              eta(f) = eta0 (f / 1 kHz)^gamma (a metal's highs die as its lows do; wood's, rubber's much faster),
//              its typical stiffness (the partials stretched) and irregularity (no real object is perfect)
//   EXCITATION what goes in: the operator's input (the FM matrix: the other operators are the mallets, the bows), its
//              own oscillator (OSC MIX), a STRIKE at every note (a raised-cosine mallet: HARDNESS its contact time)
// The operator's envelope shapes the excitation, never the ring: the object decays by itself (its voice ends when it
// is silent). A soft saturation at its output (a driven object saturates too): a resonance driven without end, an
// oscillator at the very pitch of a mode, cannot run away. Plain C++, no allocation.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace usina::fm {

constexpr int kModalModes = 64;
enum ModalStructure { MS_STRING, MS_TUBE_OPEN, MS_TUBE_CLOSED, MS_BAR, MS_TINE, MS_MEMBRANE, MS_PLATE, MS_SHEET,
                      MS_BELL, MS_BOWL, MS_SPRING, MS_BOX, kModalStructures };
enum ModalMaterial { MM_STEEL, MM_BRONZE, MM_BRASS, MM_ALUMINIUM, MM_CAST_IRON, MM_GLASS, MM_CERAMIC, MM_STONE,
                     MM_CONCRETE, MM_HARDWOOD, MM_SOFTWOOD, MM_BAMBOO, MM_PLASTIC, MM_RUBBER, MM_SKIN, MM_NYLON,
                     MM_CARDBOARD, kModalMaterials };

inline const char* modalStructureName(int s)
{
    static const char* const N[kModalStructures] = { "STRING", "TUBE OPEN", "TUBE CLOSED", "BAR", "TINE", "MEMBRANE",
                                                     "PLATE", "SHEET", "BELL", "BOWL", "SPRING", "BOX" };
    return N[((s % kModalStructures) + kModalStructures) % kModalStructures];
}
inline const char* modalMaterialName(int m)
{
    static const char* const N[kModalMaterials] = { "STEEL", "BRONZE", "BRASS", "ALUMINIUM", "CAST IRON", "GLASS",
                                                    "CERAMIC", "STONE", "CONCRETE", "HARDWOOD", "SOFTWOOD", "BAMBOO",
                                                    "PLASTIC", "RUBBER", "SKIN", "NYLON", "CARDBOARD" };
    return N[std::clamp(m, 0, kModalMaterials - 1)];
}

// a material: its loss factor at 1 kHz, how the loss grows with frequency, its typical stiffness (-1..1: how far
// its partials stretch) and irregularity (0..1). The loss factors: the literature's orders of magnitude (metals
// 1e-4..1e-3, wood ~1e-2, rubber ~1e-1), set by known decays (a steel glockenspiel bar at 2 kHz rings ~3.7 s, a
// bronze bell's hum at 300 Hz ~50 s, a hardwood marimba bar at 500 Hz ~1 s, a drum skin at 100 Hz ~0.5 s)
struct Material { double eta, gamma, stiff, irreg; };
inline const Material& modalMaterial(int m)
{
    static const Material M[kModalMaterials] = {
        { 3.0e-4, 0.0, 0.10, 0.05 },   // STEEL
        { 1.5e-4, 0.1, 0.05, 0.10 },   // BRONZE
        { 4.0e-4, 0.1, 0.05, 0.08 },   // BRASS
        { 1.0e-4, 0.0, 0.08, 0.04 },   // ALUMINIUM
        { 2.0e-3, 0.2, 0.15, 0.20 },   // CAST IRON
        { 6.0e-4, 0.3, 0.20, 0.06 },   // GLASS
        { 1.5e-3, 0.3, 0.20, 0.15 },   // CERAMIC
        { 2.0e-3, 0.4, 0.25, 0.25 },   // STONE
        { 4.0e-2, 0.5, 0.30, 0.40 },   // CONCRETE
        { 6.0e-3, 0.6, 0.00, 0.10 },   // HARDWOOD
        { 1.5e-2, 0.7, 0.00, 0.15 },   // SOFTWOOD
        { 8.0e-3, 0.6, 0.00, 0.12 },   // BAMBOO
        { 2.0e-2, 0.5, 0.00, 0.20 },   // PLASTIC
        { 1.2e-1, 0.3, 0.00, 0.30 },   // RUBBER
        { 1.5e-1, 0.5, 0.00, 0.20 },   // SKIN
        { 2.2e-3, 0.6, 0.02, 0.03 },   // NYLON
        { 6.0e-2, 0.6, 0.00, 0.35 },   // CARDBOARD
    };
    return M[std::clamp(m, 0, kModalMaterials - 1)];
}

// the structures' modes: frequency ratios to the first (ascending), and each mode's "index" for where it is struck
// (its spatial frequency along the object: sin(pi x index) weighs it). Worked out once, from the physics
struct ModalTable {
    std::array<std::array<double, kModalModes>, kModalStructures> ratio {};
    std::array<std::array<double, kModalModes>, kModalStructures> index {};
};

namespace detail {
// libc++ in Emscripten does not implement the C++ special functions. The orders here are integers;
// musl's jn evaluates the same J_m, retaining the zero search and its precision (no fitted modal ratios).
// The Android NDK's libc++ neither (27/10, the native audio engine; the owner authorized this line): its libm's jn,
// the same fdlibm-family evaluation as the WebAssembly's - the app native and the web compute the same modes.
inline double besselJ(int m, double x)
{
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    return ::jn(m, x);
#else
    return std::cyl_bessel_j((double)m, x);
#endif
}
// the m-th order Bessel function's first `count` zeros: J_m scanned from x = m (its zeros all lie above m) in steps
// of 0.05, each sign change narrowed by bisection (std::cyl_bessel_j). (Newton's method from McMahon's estimate
// wandered off for the higher orders: the membrane's ratios came out below 1, even negative - ModalTest)
inline int besselZeros(int m, int count, double* out)
{
    int found = 0;
    double a = std::max(0.5, (double)m), fa = besselJ(m, a);
    for (double b = a + 0.05; found < count && b < m + 6.0 * count + 20.0; b += 0.05) {
        const double fb = besselJ(m, b);
        if ((fa < 0.0) != (fb < 0.0)) {
            double lo = a, hi = b, flo = fa;
            for (int it = 0; it < 60; ++it) {
                const double mid = 0.5 * (lo + hi), fm = besselJ(m, mid);
                if ((flo < 0.0) == (fm < 0.0)) { lo = mid; flo = fm; } else hi = mid;
            }
            out[found++] = 0.5 * (lo + hi);
        }
        a = b;
        fa = fb;
    }
    return found;
}
inline ModalTable buildModalTable()
{
    constexpr double kPi = 3.14159265358979323846;
    ModalTable t;
    const auto put = [&](int s, int n, double r, double idx) { t.ratio[(size_t)s][(size_t)n] = r; t.index[(size_t)s][(size_t)n] = idx; };
    for (int n = 0; n < kModalModes; ++n) {
        put(MS_STRING, n, n + 1.0, n + 1.0);
        put(MS_TUBE_OPEN, n, n + 1.0, n + 1.0);
        put(MS_TUBE_CLOSED, n, 2.0 * n + 1.0, n + 0.5);
        // beams (Euler-Bernoulli): f ~ beta^2; free-free beta L = 4.7300, 7.8532 ... -> (2n + 3) pi / 2; clamped-free
        // 1.8751, 4.6941 ... -> (2n + 1) pi / 2
        static const double ff[5] = { 4.730040745, 7.853204624, 10.99560784, 14.13716549, 17.27875966 };
        static const double cf[5] = { 1.875104069, 4.694091133, 7.854757438, 10.99554073, 14.13716839 };
        const double bf = n < 5 ? ff[n] : (2.0 * n + 3.0) * kPi / 2.0, bc = n < 5 ? cf[n] : (2.0 * n + 1.0) * kPi / 2.0;
        put(MS_BAR, n, (bf / ff[0]) * (bf / ff[0]), n + 1.5);
        put(MS_TINE, n, (bc / cf[0]) * (bc / cf[0]), n + 0.5);
        // the ring (a bowl, a glass: its n = 2, 3 ... bending modes) sqrt(n^2 (n^2 - 1)^2 / (n^2 + 1))
        const auto ring = [](double k) { return std::sqrt(k * k * (k * k - 1) * (k * k - 1) / (k * k + 1)); };
        put(MS_BOWL, n, ring(n + 2.0) / ring(2.0), n + 1.0);
        // a coil spring: strongly dispersive (its partials spread ~ n^1.85)
        put(MS_SPRING, n, std::pow(n + 1.0, 1.85), n + 1.0);
    }
    // the membrane: the zeros of J_m (m = 0..), sorted, over J_0's first
    {
        std::array<double, 400> z {};
        int c = 0;
        for (int m = 0; m < 40 && c + 10 <= 400; ++m) c += besselZeros(m, 10, z.data() + c);
        std::sort(z.begin(), z.begin() + c);
        for (int n = 0; n < kModalModes; ++n) put(MS_MEMBRANE, n, z[(size_t)n] / z[0], std::sqrt(z[(size_t)n] / z[0]) * 2.0);
    }
    // plates (simply supported, sides 1 : a): (m^2 + (n a)^2) over the first; PLATE a = 1.13 (the square's twin
    // modes split a little), SHEET a = 1.618 (a long sheet: denser, less regular)
    for (const auto& [s, a] : { std::pair { (int)MS_PLATE, 1.13 }, std::pair { (int)MS_SHEET, 1.618 } }) {
        std::array<double, 400> f {};
        int c = 0;
        for (int m = 1; m <= 20; ++m)
            for (int k = 1; k <= 20; ++k) f[(size_t)c++] = m * m + (k * a) * (k * a);
        std::sort(f.begin(), f.begin() + c);
        for (int n = 0; n < kModalModes; ++n) put(s, n, f[(size_t)n] / f[0], std::sqrt(f[(size_t)n] / f[0]) * 1.5);
    }
    // a box's air (a rectangular cavity, 1 : 1.26 : 1.59): sqrt(l^2 + (m / 1.26)^2 + (n / 1.59)^2), all but 0, 0, 0
    {
        std::array<double, 1000> f {};
        int c = 0;
        for (int l = 0; l < 10; ++l)
            for (int m = 0; m < 10; ++m)
                for (int k = 0; k < 10; ++k)
                    if (l + m + k > 0) f[(size_t)c++] = std::sqrt(l * l + (m / 1.26) * (m / 1.26) + (k / 1.59) * (k / 1.59));
        std::sort(f.begin(), f.begin() + c);
        for (int n = 0; n < kModalModes; ++n) put(MS_BOX, n, f[(size_t)n] / f[0], f[(size_t)n] / f[0]);
    }
    // a well-tuned church bell over its hum: hum 1, prime 2, tierce 2.378, quint 2.997, nominal 4 (Perrin, Charnley
    // and de Pont 1982); above the nominal, a typical upper series (to be measured on real bells), then x1.115 a mode
    {
        static const double b[13] = { 1.0, 2.0, 2.378, 2.997, 4.0, 5.33, 5.98, 6.66, 8.0, 9.33, 10.6, 11.9, 13.3 };
        for (int n = 0; n < kModalModes; ++n)
            put(MS_BELL, n, n < 13 ? b[n] : t.ratio[MS_BELL][(size_t)n - 1] * 1.115, std::sqrt(n < 13 ? b[n] : 13.3) * 2.0);
    }
    return t;
}
} // namespace detail

inline const ModalTable& modalTable()
{
    static const ModalTable t = detail::buildModalTable();
    return t;
}

// the settings of one MODAL operator this control block (from its values plus the routes)
struct ModalSettings {
    int structure = MS_STRING, material = MM_STEEL;
    double f1 = 220.0;          // the first mode, Hz (the operator's pitch moved by SIZE)
    double decay = 1.0;         // x the material's T60 (DECAY)
    double bright = 0.0;        // -1..1: the highs dying faster (-) or slower (+) than the material says
    double stiff = 0.0;         // -1..1: added to the material's: the partials stretched (+) or squeezed (-)
    double position = 0.3;      // 0..1: where it is struck
    int density = 32;           // modes
    double irreg = 0.0;         // 0..1: added to the material's
    double strike = 0.8;        // 0..1: the mallet's level at a note
    double hardness = 0.6;      // 0..1: the mallet's (and the input's) brightness
    int exciter = 0;            // stage 2: 0 STRIKE, 1 BOW, 2 BLOW, 3 SCRAPE (what the excitation becomes)
    double pressure = 0.5;      // 0..1: the bow's pressure, the breath's turbulence, the scrape's rate
    double tension = 0.0;       // 0..1: the pitch rising with the amplitude
    double rattle = 0.0;        // 0..1: a contact buzzing on the peaks
};
enum ModalExciter { MX_STRIKE, MX_BOW, MX_BLOW, MX_SCRAPE, kModalExciters };
inline const char* modalExciterName(int e)
{
    static const char* const N[kModalExciters] = { "STRIKE", "BOW", "BLOW", "SCRAPE" };
    return N[std::clamp(e, 0, kModalExciters - 1)];
}

// one MODAL operator's bank, for one voice
struct ModalBank {
    std::array<double, kModalModes> g {}, a1 {}, a2 {}, a3 {}, amp {}, ic1 {}, ic2 {};
    int n = 0;                                        // modes in use (below 20 kHz and the rate's 0.45)
    double norm = 1.0, lp = 0.0, lpK = 1.0, level = 0.0, levelK = 0.0;
    double strikeLeft = 0.0, strikeLen = 1.0, strikeAmp = 0.0;   // the mallet's pulse: samples left, length, height
    uint32_t seed = 1;
    // stage 2: the exciter, its pressure; the scrape's chance of a grain a sample; the buzz's threshold and depth
    int exciter = MX_STRIKE;
    double pressure = 0.5, grainChance = 0.0, rattle = 0.0, rattleTh = 1.0, last = 0.0;
    uint32_t rng = 0x9E3779B9u;
    // the bank's own rate: the engine's / div (the bank is linear and below 20 kHz: oversampled N x, it runs at 2 x the
    // host's, the excitation averaged over div samples, its sound held between - each mode's amplitude corrected for
    // what the averaging and the hold take off at its frequency, so the result is flat; the decimator after takes the
    // images away). div 1: every sample, as before
    int div = 1, cnt = 0;
    double acc = 0.0, accEnv = 0.0, held = 0.0;

    void clear()
    {
        ic1.fill(0.0);
        ic2.fill(0.0);
        lp = level = last = 0.0;
        strikeLeft = 0.0;
        cnt = 0;
        acc = accEnv = held = 0.0;
    }
    double noise()                                    // (-1..1, xorshift)
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (rng >> 8) / 8388608.0 - 1.0;
    }

    // a note: the mallet comes down (its contact time from HARDNESS: 0.1 ms hard .. 8 ms soft; its area kept, so a
    // soft mallet is not louder, only darker)
    void strikeNote(const ModalSettings& s, double fs, double velocity)
    {
        strikeLen = std::max(1.0, fs * (0.0001 + 0.008 * (1.0 - s.hardness) * (1.0 - s.hardness)));
        strikeLeft = strikeLen;
        strikeAmp = s.strike * velocity * 2.0 / strikeLen;
    }

    // this control block's modes from the settings; fs the engine's rate, band the audible top (20 kHz); bankDiv the
    // engine's samples a bank step (the engine oversampled N x: N / 2, the bank at 2 x the host's)
    void update(const ModalSettings& s, double fs, double bandTop, int opSalt, int bankDiv = 1)
    {
        constexpr double kPi = 3.14159265358979323846, kLn1000 = 6.907755278982137;
        const auto& tb = modalTable();
        const int st = ((s.structure % kModalStructures) + kModalStructures) % kModalStructures;
        const Material& mt = modalMaterial(s.material);
        const double stiff = std::clamp(mt.stiff + s.stiff, -1.0, 1.0);
        const double B = stiff * std::fabs(stiff) * 0.004;            // (a stiff string's B, signed: squeezed below 0)
        const double irr = std::clamp(mt.irreg + s.irreg, 0.0, 1.0);
        const double gamma = mt.gamma - s.bright;                     // BRIGHT tilts how the loss grows with f
        if (bankDiv != div) { div = std::max(1, bankDiv); cnt = 0; acc = accEnv = 0.0; }
        const double fb = fs / div;                                   // (the bank's rate)
        const double top = std::min(bandTop, 0.45 * fb);
        const int want = std::clamp(s.density, 1, kModalModes);
        // TENSION: a struck membrane or gong is stretched by its own swing: the pitch up with the level (up to ~2
        // semitones at full), gliding down as it decays (the level followed over 50 ms; once a block)
        const double f1 = s.f1 * (1.0 + 0.12 * std::clamp(s.tension, 0.0, 1.0) * std::min(1.0, level * 2.0));
        exciter = std::clamp(s.exciter, 0, kModalExciters - 1);
        pressure = std::clamp(s.pressure, 0.0, 1.0);
        grainChance = (20.0 * std::pow(100.0, pressure)) / fs;        // SCRAPE: 20 .. 2000 grains a second
        rattle = std::clamp(s.rattle, 0.0, 1.0);
        rattleTh = 0.35 * (1.0 - 0.8 * rattle);                        // (the louder the rattle, the sooner it buzzes)
        const double x = 0.02 + 0.96 * std::clamp(s.position, 0.0, 1.0);
        int m = 0;
        double sumAmp = 0.0;
        for (int i = 0; i < want; ++i) {
            const double r0 = tb.ratio[(size_t)st][(size_t)i];
            double r = r0 * std::sqrt(std::max(0.05, 1.0 + B * (r0 * r0 - 1.0)));
            // irregularity: each mode a little off, the same every note (the object, not a dice roll): a hash of the
            // mode and the operator, up to +-3 % x IRREG
            uint32_t h = (uint32_t)(i * 2654435761u) ^ (uint32_t)(opSalt * 40503u + 0x9E3779B9u);
            h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
            const double u = (h >> 8) / 8388608.0 - 1.0;
            if (i > 0) r *= 1.0 + 0.03 * irr * u;
            const double f = f1 * r;
            if (f >= top || f <= 0.0) break;                          // (the rest are above: ascending)
            const double eta = mt.eta * std::pow(std::max(f, 20.0) / 1000.0, gamma);
            const double t60 = std::clamp(kLn1000 / (kPi * f * eta) * s.decay, 0.002, 60.0);
            const double q = kPi * f * t60 / kLn1000;                // (Q = pi f T60 / ln 1000; for constant eta: 1 / eta)
            const double gg = std::tan(kPi * f / fb), k = 1.0 / q;
            g[(size_t)m] = gg;
            a1[(size_t)m] = 1.0 / (1.0 + gg * (gg + k));
            a2[(size_t)m] = gg * a1[(size_t)m];
            a3[(size_t)m] = gg * a2[(size_t)m];
            // its loudness: where it is struck (sin(pi x index)), a little irregular too; per unit impulse the band-
            // pass rings at ~2 g: / g makes every mode ring alike whatever its pitch and Q. A bank step every div
            // samples: the averaged impulse is div times lower (x div), and the averaging and the hold each take
            // sin(pi f div / fs) / (div sin(pi f / fs)) off at f (/ that, twice: flat)
            const double w = std::fabs(std::sin(kPi * x * tb.index[(size_t)st][(size_t)i])) * (1.0 + 0.3 * irr * u);
            const double droop = div > 1 ? std::sin(kPi * f * div / fs) / (div * std::sin(kPi * f / fs)) : 1.0;
            amp[(size_t)m] = 0.5 * w * div / gg / (droop * droop);
            sumAmp += w * w;
            ++m;
        }
        for (int i = m; i < n; ++i) ic1[(size_t)i] = ic2[(size_t)i] = 0.0;   // (a mode that left starts again from rest)
        n = m;
        norm = 1.0 / std::sqrt(std::max(sumAmp, 1.0));
        // the excitation's low-pass (HARDNESS: 300 Hz soft .. 18 kHz hard), a one-pole
        const double fc = 300.0 * std::pow(60.0, std::clamp(s.hardness, 0.0, 1.0));
        lpK = 1.0 - std::exp(-2.0 * kPi * std::min(fc, top) / fs);
        levelK = 1.0 - std::exp(-1.0 / (0.05 * fb));                  // (the ring's level, followed over 50 ms)
    }

    // one sample: the excitation in (the operator's input and oscillator, already shaped by its envelope), the
    // envelope itself (env: a bow's speed, a breath, a scrape's hand), the object's sound out (soft-saturated at +-1)
    double process(double excite, double env)
    {
        constexpr double kPi = 3.14159265358979323846;
        switch (exciter) {                                            // what the hand does to the object (the bow: below)
        case MX_BLOW:                                                 // a breath: turbulence, its strength PRESSURE
            excite += env * (0.05 + 0.6 * pressure) * noise();
            break;
        case MX_SCRAPE:                                               // a hand dragged: grains at random, PRESSURE many
            if (env > 0.0 && 0.5 * (noise() + 1.0) < grainChance) excite += env * 3.0 * noise();
            break;
        default: break;
        }
        if (strikeLeft > 0.0) {                                       // the mallet: a raised cosine
            excite += strikeAmp * 0.5 * (1.0 - std::cos(2.0 * kPi * (strikeLen - strikeLeft) / strikeLen));
            strikeLeft -= 1.0;
        }
        lp += (excite - lp) * lpK;
        // the bank's step: every div samples, on their average (in between, its last sound held)
        acc += lp;
        accEnv += env;
        if (++cnt < div) return held;
        cnt = 0;
        double v0 = acc / div;
        const double envB = accEnv / div;
        acc = accEnv = 0.0;
        if (exciter == MX_BOW) {
            // stick-slip: the bow moves at its speed (the envelope, plus what comes in), the object at its own (its
            // last sound: the modes are band-passes, their output a velocity); the force is the friction curve of
            // the difference - near 0 the bow grips, past it it slips (the bowed-string table of STK, as a reference:
            // x (|x| slope + 0.75)^-4). More PRESSURE: a firmer grip, a stronger push
            const double dv = envB * 0.5 + v0 - last, slope = 5.0 - 4.0 * pressure;
            v0 = dv * std::min(1.0, std::pow(std::fabs(dv) * slope + 0.75, -4.0)) * (0.5 + pressure);
        }
        double y = 0.0;
        for (int i = 0; i < n; ++i) {                                 // (Zavalishin's TPT state-variable band-pass)
            const double v3 = v0 - ic2[(size_t)i];
            const double v1 = a1[(size_t)i] * ic1[(size_t)i] + a2[(size_t)i] * v3;
            const double v2 = ic2[(size_t)i] + a2[(size_t)i] * ic1[(size_t)i] + a3[(size_t)i] * v3;
            ic1[(size_t)i] = 2.0 * v1 - ic1[(size_t)i];
            ic2[(size_t)i] = 2.0 * v2 - ic2[(size_t)i];
            y += amp[(size_t)i] * v1;
        }
        y *= norm;
        if (!std::isfinite(y)) { clear(); y = 0.0; }
        last = y;
        if (rattle > 0.0) {                                           // RATTLE: past its threshold, the contact buzzes
            const double over = std::fabs(y) - rattleTh;
            if (over > 0.0) y += rattle * over * 4.0 * noise();
        }
        const double out = y / std::sqrt(1.0 + y * y);                // (soft saturation: x / sqrt(1 + x^2))
        level += (std::fabs(out) - level) * levelK;
        held = out;
        return out;
    }
};

} // namespace usina::fm
