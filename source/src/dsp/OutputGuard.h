// SPDX-License-Identifier: GPL-3.0-only
// Usina - the guard at the end of the chain (2026-10-20, the owner: "a mechanism to avoid overload ... and protect
// the hearing at the end of the chain"). It cannot be bypassed: every sample Usina outputs goes through it.
//
// In order, per sample (docs/SEGURANCA.md, its layers):
//   1. sanitise   a NaN or an infinity becomes silence, and is counted (a runaway filter, a bad division);
//   2-3. DC and   a 2nd-order Butterworth high-pass at 16 Hz: DC (loops and asymmetric shapers pile it up) and a
//   subsonic      loop's infrasound (a woofer at its excursion's end, unheard) never leave; -0.34 dB at 30 Hz,
//                 -0.07 dB at 45 Hz, -12 dB at 8 Hz (the playable bass untouched: GuardTests 1b);
//   4. ultrasonic above a 50 kHz host rate, a 16th-order Butterworth low-pass (-0.1 dB at 20 kHz, -40 dB at 30 kHz):
//                 a loop screaming at 25-40 kHz, unheard, never reaches a tweeter;
//   5. runaway    a peak past +12 dBFS before limiting is an explosion, not music: the output fades to silence in
//                 10 ms, stays silent 300 ms, then comes back over 1 s; the processor is told (to reset loops);
//   6. limiter    a TRUE-PEAK look-ahead limiter: its detector sees the signal 4x oversampled (a windowed sinc, as
//                 ITU-R BS.1770's meters), so the peaks between the samples - the ones a D/A converter rebuilds -
//                 are held at the ceiling too (in dBTP). Stereo-linked; its gain a min-filter of the needed gains
//                 averaged over the look-ahead (smooth, never late), aligned to the detector's own delay;
//   7. loudness   after the limiter, on what it lets out: K-weighted (ITU-R BS.1770, Loudness.h), the short-term
//                 (3 s) held under -9 LUFS (at 2.5 dB/s), the momentary (400 ms) under -5 LUFS (6 dB/s);
//                 ANTI-STARTLE: a fast (50 ms) jump 25 LU above the background (the 3 s before the last 300 ms) and
//                 past -3 LUFS (a burst out of silence) held at the larger of the two in ~10 ms, lifting (30 dB/s)
//                 as the background takes it in. Numbers from tests/LoudnessLab: the library played normally stays
//                 under all three (its loudest: -13.3 short, -7.7 momentary, -4.2 fast);
//   8. clamp      a last hard clamp of the samples at the ceiling, whatever happened before.
// No allocation and no locks in process(); prepare() sizes everything.
#pragma once
#include "Loudness.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace usina {

class OutputGuard {
public:
    static constexpr int kMaxChannels = 2;

    void prepare(double sampleRate, int maxBlock);
    void reset();
    // Processes in place. ceilingDb (dBTP) in [-24, -0.3]; the processor clamps it.
    void process(float* const* channels, int numChannels, int numSamples, float ceilingDb);

    // The look-ahead delay, for setLatencySamples (rule 8: the host compensates it)
    int latencySamples() const { return delayLen; }

    // For the interface (read from any thread)
    float gainReductionDb() const { return grDb.load(std::memory_order_relaxed); }
    float loudnessCutDb() const { return loudCutDb.load(std::memory_order_relaxed); }
    uint32_t runawayEvents() const { return runaways.load(std::memory_order_relaxed); }
    uint32_t nonFiniteEvents() const { return nonFinite.load(std::memory_order_relaxed); }
    // True once after a runaway (the processor resets its feedback paths and voices)
    bool takeRunawayFlag() { return runawayFlag.exchange(false, std::memory_order_acq_rel); }

    // the true-peak interpolator: 4 phases of kTpTaps taps each; the points it finds at a sample lie between the
    // samples kTpDelay and kTpDelay - 1 back. 32 a phase: full-band white noise at 48 kHz under-read by 0.09 dB
    // against a 64-tap meter (16: by 0.8 dB, the content near Nyquist lost in a short filter's transition)
    static constexpr int kTpTaps = 32, kTpDelay = kTpTaps / 2;
    // the gain held flat this many samples each side of a peak: the sinc rebuilding a peak between the samples also
    // weighs its neighbours, and a gain still ramping down on them let full-band noise past by 0.3 dB
    static constexpr int kTpFlat = 8;

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[kMaxChannels] {}, z2[kMaxChannels] {};
        double step(int c, double x)                     // (transposed direct form II)
        {
            const double y = b0 * x + z1[c];
            z1[c] = b1 * x - a1 * y + z2[c];
            z2[c] = b2 * x - a2 * y;
            return y;
        }
        void clear() { for (int c = 0; c < kMaxChannels; ++c) z1[c] = z2[c] = 0.0; }
    };
    static constexpr int kLpSections = 8;

    double fs = 48000.0;
    int look = 72;                       // look-ahead in samples (1.5 ms)
    int delayLen = 80;                   // the delay line: the look-ahead plus the detector's delay
    // the protection filters
    Biquad hp;
    std::array<Biquad, kLpSections> lp {};
    bool lpOn = false;
    // the true-peak detector: its taps [phase][tap]; each channel's last kTpTaps samples, written twice (a window
    // read without wrapping)
    std::array<std::array<float, kTpTaps>, 4> tpCoef {};
    std::array<std::array<float, 2 * kTpTaps>, kMaxChannels> tpHist {};
    int tpPos = 0;
    // the delay line the limiter looks ahead through
    std::vector<float> delay[kMaxChannels];
    int dpos = 0;
    // min-filter of needed gains (monotonic deque over minLen) and its running average (over look)
    int minLen = 74;
    std::vector<float> dqVal;
    std::vector<int> dqIdx;
    int dqHead = 0, dqTail = 0, tick = 0;
    std::vector<float> avgBuf;
    double avgSum = 0.0;
    int avgPos = 0;
    float gain = 1.0f, relCoef = 0.0f;
    // runaway: panic fade
    enum class Panic { Off, Falling, Holding, Rising } panic = Panic::Off;
    float panicGain = 1.0f;
    int panicCount = 0;
    // loudness governor: the meter, its gain (dB) and where it goes, at what rate (dB/s); as a factor
    Loudness meter;
    double govDb = 0.0, govTarget = 0.0, govRate = 6.0, govUp = 6.0;
    bool govStartled = false;                    // (coming back from a startle: up fast)
    float loudGain = 1.0f;

    std::atomic<float> grDb { 0.0f }, loudCutDb { 0.0f };
    std::atomic<uint32_t> runaways { 0 }, nonFinite { 0 };
    std::atomic<bool> runawayFlag { false };
};

} // namespace usina
