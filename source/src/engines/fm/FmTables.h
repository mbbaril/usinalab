// SPDX-License-Identifier: GPL-3.0-only
// Usina FM engine - the tables. The same formulas as the firmware's generator (usina16 tools/gen_opx_tables.py,
// tools/gen_tables.py), computed here in floating point at start-up: the waves, the envelope times, the coarse
// ratios, the 40 algorithms. Plain C++, no JUCE.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace usina::fm {

constexpr int kTableN = 1024;                    // points a cycle (+1 guard point)
constexpr int kNumWaves = 33;                    // OPX's: 19 tables, then 4 noises; the chip sines, 4 noises (22/10)
constexpr int kNoise0 = 19;                      // NOISE S/H, WHITE, PINK, BLUE
constexpr int kNumCoarse = 39;                   // 1/128 .. 1/2, 1 .. 32
constexpr int kNumAlgs = 40;                     // 1..32 the classic ones, 33..40 OPX's; 41 (index 40) = the matrix
constexpr int kHdLevels = 9;                     // SAW HD, SQR HD: band-limited levels

// the chip sines (22/10; the user's 15/10 request, the 2-operator FM chips' waves - the shapes from the YMF262's, as
// Nuked-OPL3 documents them, our own code): HALF SIN (the second half silent), ABS SIN (both halves up), QTR SIN (the
// rising quarters only), ALT SIN (twice as fast in the first half, silent in the second), CAMEL SIN (that, both
// humps up), LOG SAW (a spike falling exponentially, 2^-16p, then its mirror, down). Band-limited as SAW HD (the
// levels by pitch), their DC kept: a half sine modulating pulls the pitch, as on the chip. (Its square: SQR HD)
// the noises after them (22/10; the user, after SuperCollider's noises - the ideas from its documentation, our code):
// BROWN (a random walk, reflected at the edges), GREY (a word, one random bit flipped a sample: loud for its peak,
// heavier below), CLIP (only -1 and +1), RAMP (as S/H, a new value each cycle of the pitch, but gliding to it: a noise
// with a pitch and no steps)
enum Wave { SIN, SIN12, SIN8, TRI, SAW, SAWHD, SQR, SQRHD, ASAW3, ASQR3, ATRI3, A12345, A1P2, A1P3, A1P4, A1P5,
            A1P6, A1P7, A1P8, NSH, NWHITE, NPINK, NBLUE, HALFSIN, ABSSIN, QTRSIN, ALTSIN, CAMELSIN, LOGSAW,
            NBROWN, NGREY, NCLIP, NRAMP };
inline bool isNoiseWave(int w) { return (w >= NSH && w <= NBLUE) || w >= NBROWN; }
constexpr int kChipWaves = 6;                    // HALFSIN .. LOGSAW
enum Mode { FM, RING, FILTER, FILTFM, FOLD, EFFECT, BYPASS, MUTE, MODAL, CHAOS, STOCH, MACRO };   // (MODAL..: Usina's)

struct Tables {
    std::array<std::array<float, kTableN + 1>, kNoise0> wave {};          // SAW HD, SQR HD hold the plain ones
    std::array<std::array<float, kTableN + 1>, kHdLevels> hdSaw {}, hdSqr {};
    std::array<std::array<std::array<float, kTableN + 1>, kHdLevels>, kChipWaves> hdChip {};   // (HALFSIN ..)
    std::array<double, 128> envMs {};            // envelope time index -> ms (0 = 0 ms; 1..127: 1 ms .. 90 s)
    std::array<double, kNumCoarse> coarseLog2 {};
    // msfa's encoding, per operator, the SIXTH first: in bus << 4 | add 4 | out bus (0 = the output)
    std::array<std::array<uint8_t, 6>, kNumAlgs> alg {};

    static const Tables& get();                  // built once, thread-safe (a function-local static)

private:
    Tables();
};

// one cycle of a table at phase (cycles, any real), linear between the points (as the firmware's opx_w)
inline float lookup(const std::array<float, kTableN + 1>& t, double phase)
{
    const double f = (phase - (double)(int64_t)phase + 1.0) * kTableN;   // (+1: a negative phase wraps up)
    const int i = (int)f & (kTableN - 1);
    const float fr = (float)(f - (double)(int64_t)f);
    return t[(size_t)i] + fr * (t[(size_t)i + 1] - t[(size_t)i]);
}

} // namespace usina::fm
