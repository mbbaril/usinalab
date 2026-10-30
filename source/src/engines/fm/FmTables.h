// SPDX-License-Identifier: GPL-3.0-only
// Usina lookup tables: waveforms, envelope times, coarse ratios and algorithms.
// Reference formulas are evaluated in floating point at initialization.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace usina::fm {

constexpr int kTableN = 1024;                    // points a cycle (+1 guard point)
constexpr int kNumWaves = 33;                    // OPX's: 19 tables, then 4 noises; the chip sines, 4 noises
constexpr int kNoise0 = 19;                      // NOISE S/H, WHITE, PINK, BLUE
constexpr int kNumCoarse = 39;                   // 1/128 .. 1/2, 1 .. 32
constexpr int kNumAlgs = 40;                     // 1..32 the classic ones, 33..40 OPX's; 41 (index 40) = the matrix
constexpr int kHdLevels = 9;                     // SAW HD, SQR HD: band-limited levels

// Band-limited sine variants: HALF SIN silences the second half; ABS SIN
// rectifies both halves; QTR SIN retains rising quarters; ALT SIN doubles
// the first-half frequency; CAMEL SIN rectifies ALT SIN; LOG SAW uses an
// exponential spike and its inverted mirror. DC is retained for modulation.
// Noise variants: BROWN is a reflected random walk; GREY flips one random
// bit per sample; CLIP outputs +/-1; RAMP interpolates random cycle values.
enum Wave { SIN, SIN12, SIN8, TRI, SAW, SAWHD, SQR, SQRHD, ASAW3, ASQR3, ATRI3, A12345, A1P2, A1P3, A1P4, A1P5,
            A1P6, A1P7, A1P8, NSH, NWHITE, NPINK, NBLUE, HALFSIN, ABSSIN, QTRSIN, ALTSIN, CAMELSIN, LOGSAW,
            NBROWN, NGREY, NCLIP, NRAMP };
inline bool isNoiseWave(int w) { return (w >= NSH && w <= NBLUE) || w >= NBROWN; }
constexpr int kChipWaves = 6;                    // HALFSIN .. LOGSAW
enum Mode { FM, RING, FILTER, FILTFM, FOLD, EFFECT, BYPASS, MUTE, MODAL, CHAOS, STOCH, MACRO, SAMPLE, UNISON, BITS };   // (MODAL..: Usina's)

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
