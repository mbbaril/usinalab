// SPDX-License-Identifier: GPL-3.0-only
// Mandatory final stereo output guard.
//
// Processing order:
// 1. Replace non-finite samples with silence and count them.
// 2. Apply a second-order Butterworth high-pass at 16 Hz.
// 3. Above 50 kHz sample rate, apply a 16th-order low-pass at 22.5 kHz.
// 4. Peaks above +12 dBFS trigger 10 ms fade-out, 300 ms hold and 1 s recovery.
// 5. Stereo-linked true-peak limiting uses 4x interpolation and lookahead.
// 6. K-weighted loudness caps: -9 LUFS short-term at 2.5 dB/s;
//    -5 LUFS momentary at 6 dB/s. Fast transients above -3 LUFS and
//    25 LU over background use the larger threshold, with approximately 10 ms attack.
// 7. Clamp final samples to the requested ceiling.
// No allocation or locks in process().
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

    // Latency in samples for host compensation.
    int latencySamples() const { return delayLen; }

    // For the interface (read from any thread)
    float gainReductionDb() const { return grDb.load(std::memory_order_relaxed); }
    float loudnessCutDb() const { return loudCutDb.load(std::memory_order_relaxed); }
    uint32_t runawayEvents() const { return runaways.load(std::memory_order_relaxed); }
    uint32_t nonFiniteEvents() const { return nonFinite.load(std::memory_order_relaxed); }
    // True once after a runaway (the processor resets its feedback paths and voices)
    bool takeRunawayFlag() { return runawayFlag.exchange(false, std::memory_order_acq_rel); }

    // Four interpolation phases of kTpTaps taps. At output time, reconstructed
    // points lie between samples kTpDelay and kTpDelay-1 in the history.
    static constexpr int kTpTaps = 32, kTpDelay = kTpTaps / 2;
    // Hold gain flat around each peak to limit reconstructed intersample overshoot.
    static constexpr int kTpFlat = 8;

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[kMaxChannels] {}, z2[kMaxChannels] {};
        double step(int c, double x)                     // Transposed direct form II
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
    int look = 72;                       // Look-ahead in samples (1.5 ms)
    int delayLen = 80;                   // The delay line: the look-ahead plus the detector's delay
    // the protection filters
    Biquad hp;
    std::array<Biquad, kLpSections> lp {};
    bool lpOn = false;
    // The true-peak detector: its taps [phase][tap]; each channel's last kTpTaps samples, written twice (a window
    // read without wrapping)
    std::array<std::array<float, kTpTaps>, 4> tpCoef {};
    std::array<std::array<float, 2 * kTpTaps>, kMaxChannels> tpHist {};
    int tpPos = 0;
    // The delay line the limiter looks ahead through
    std::vector<float> delay[kMaxChannels];
    int dpos = 0;
    // Min-filter of needed gains (monotonic deque over minLen) and its running average (over look)
    int minLen = 74;
    std::vector<float> dqVal;
    std::vector<int> dqIdx;
    int dqHead = 0, dqTail = 0, tick = 0;
    std::vector<float> avgBuf;
    double avgSum = 0.0;
    int avgPos = 0;
    float gain = 1.0f, relCoef = 0.0f;
    // Runaway: panic fade
    enum class Panic { Off, Falling, Holding, Rising } panic = Panic::Off;
    float panicGain = 1.0f;
    int panicCount = 0;
    // Loudness governor: the meter, its gain (dB) and where it goes, at what rate (dB/s); as a factor
    Loudness meter;
    double govDb = 0.0, govTarget = 0.0, govRate = 6.0, govUp = 6.0;
    bool govStartled = false;                    // Coming back from a startle: up fast
    float loudGain = 1.0f;

    std::atomic<float> grDb { 0.0f }, loudCutDb { 0.0f };
    std::atomic<uint32_t> runaways { 0 }, nonFinite { 0 };
    std::atomic<bool> runawayFlag { false };
};

} // Namespace usina
