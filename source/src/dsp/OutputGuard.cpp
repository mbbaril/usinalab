// SPDX-License-Identifier: GPL-3.0-only
#include "OutputGuard.h"
#include <algorithm>
#include <cmath>

namespace usina {

namespace {
constexpr float kRunawayPeak = 3.981f;        // Runaway threshold before limiting, in dBFS.
constexpr double kShortCap = -9.0;            // Short-term loudness cap; attenuation rate 2.5 dB/s.
constexpr double kMomCap = -5.0;              // Momentary loudness cap; attenuation rate 6 dB/s.
constexpr double kStartleJump = 25.0;         // Fast loudness threshold above the background, in LU.
constexpr double kStartleFloor = -3.0;        // Absolute fast loudness threshold, in LUFS. Use the larger threshold.
inline float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
inline float linToDb(float g) { return 20.0f * std::log10(std::max(g, 1.0e-6f)); }
} // Namespace

void OutputGuard::prepare(double sampleRate, int /*maxBlock*/)
{
    constexpr double kPi = 3.14159265358979323846;
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    look = std::max(8, (int)std::lround(fs * 0.0015));
    // Align lookahead, interpolator delay and kTpFlat so gain reaches its target
    // before each reconstructed peak and remains flat across its neighbors.
    delayLen = look + kTpDelay + kTpFlat;
    minLen = look + 2 + 2 * kTpFlat;
    relCoef = (float)(1.0 - std::exp(-1.0 / (0.150 * fs)));           // Limiter release ~150 ms
    meter.prepare(fs);

    // Protection filters: second-order 16 Hz high-pass; above 50 kHz host rate,
    // eight Butterworth biquads form a 16th-order 22.5 kHz low-pass.
    const auto design = [&](Biquad& b, double f0, double q, bool high) {
        const double w = 2.0 * kPi * f0 / fs, cw = std::cos(w), al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
        b.b0 = (high ? (1.0 + cw) : (1.0 - cw)) / 2.0 / a0;
        b.b1 = (high ? -(1.0 + cw) : (1.0 - cw)) / a0;
        b.b2 = b.b0;
        b.a1 = -2.0 * cw / a0;
        b.a2 = (1.0 - al) / a0;
    };
    design(hp, 16.0, 0.70710678, true);
    lpOn = fs > 50000.0;
    for (int k = 0; k < kLpSections; ++k)
        design(lp[(size_t)k], 22500.0, 1.0 / (2.0 * std::cos((2.0 * k + 1.0) * kPi / (4.0 * kLpSections))), false);

    // The true-peak interpolator: a 4x low-pass (a sinc at the input's Nyquist, Kaiser beta 5), split in 4 phases,
    // each phase's taps summing to 1 (a constant reads as itself). Centred on a whole sample, so the points fall at
    // 0, 1/4, 1/2, 3/4 between two samples (as the BS.1770 meters: the classic fs/4 overshoot lies at 1/2)
    const double c = 2.0 * kTpTaps - 1.0, beta = 5.0;
    const auto i0 = [](double x) {                       // The modified Bessel function, by its series
        double s = 1.0, t = 1.0;
        for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; }
        return s;
    };
    for (int p = 0; p < 4; ++p) {
        double sum = 0.0;
        for (int k = 0; k < kTpTaps; ++k) {
            const double m = 4.0 * k + p - c, u = m / 4.0, r = m / (c + 1.0);
            const double h = (std::fabs(u) < 1e-12 ? 1.0 : std::sin(kPi * u) / (kPi * u)) *
                             i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0(beta);
            tpCoef[(size_t)p][(size_t)k] = (float)h;
            sum += h;
        }
        for (auto& t : tpCoef[(size_t)p]) t = (float)(t / sum);
    }

    for (auto& d : delay)
        d.assign((size_t)delayLen, 0.0f);
    dqVal.assign((size_t)minLen + 1, 1.0f);
    dqIdx.assign((size_t)minLen + 1, 0);
    avgBuf.assign((size_t)look, 1.0f);
    reset();
}

void OutputGuard::reset()
{
    for (int c = 0; c < kMaxChannels; ++c) {
        std::fill(delay[c].begin(), delay[c].end(), 0.0f);
        tpHist[(size_t)c].fill(0.0f);
    }
    hp.clear();
    for (auto& b : lp) b.clear();
    tpPos = 0;
    dpos = 0;
    dqHead = dqTail = 0;
    tick = 0;
    std::fill(avgBuf.begin(), avgBuf.end(), 1.0f);
    avgSum = (double)look;
    avgPos = 0;
    gain = 1.0f;
    panic = Panic::Off;
    panicGain = 1.0f;
    panicCount = 0;
    meter.reset();
    govDb = govTarget = 0.0;
    govRate = govUp = 6.0;
    govStartled = false;
    loudGain = 1.0f;
}

void OutputGuard::process(float* const* ch, int numChannels, int n, float ceilingDb)
{
#ifdef USINA_TEST_GUARD_OFF
    // Only in the test library usina_dsp_noguard: the safety test built on a guard that does nothing must fail
    (void)ch; (void)numChannels; (void)n; (void)ceilingDb;
#else
    const int nc = std::min(numChannels, kMaxChannels);
    const float ceiling = dbToLin(std::clamp(ceilingDb, -24.0f, -0.3f));
    const int dqCap = (int)dqVal.size();
    const float panicFall = 1.0f / (float)(0.010 * fs), panicRise = 1.0f / (float)(1.0 * fs);
    const int panicHold = (int)(0.300 * fs);
    float minGain = 1.0f;

    for (int i = 0; i < n; ++i) {
        // 1. sanitise; the stereo-linked peak of this incoming sample (after the filters)
        float peak = 0.0f, x[kMaxChannels] {};
        for (int c = 0; c < nc; ++c) {
            float s = ch[c][i];
            if (!std::isfinite(s)) {
                s = 0.0f;
                nonFinite.fetch_add(1, std::memory_order_relaxed);
            }
            // 2-4. the protection filters: DC and infrasound, ultrasound (a state gone non-finite starts from rest)
            double f = hp.step(c, s);
            if (lpOn)
                for (auto& b : lp) f = b.step(c, f);
            if (!std::isfinite(f)) {
                f = 0.0;
                hp.clear();
                for (auto& b : lp) b.clear();
            }
            x[c] = (float)f;
            peak = std::max(peak, std::fabs(x[c]));
        }
        // 5. runaway: an explosion fades out, holds, comes back
        if (peak > kRunawayPeak && panic == Panic::Off) {
            panic = Panic::Falling;
            runaways.fetch_add(1, std::memory_order_relaxed);
            runawayFlag.store(true, std::memory_order_release);
        }
        switch (panic) {
        case Panic::Falling:
            panicGain -= panicFall;
            if (panicGain <= 0.0f) { panicGain = 0.0f; panic = Panic::Holding; panicCount = panicHold; }
            break;
        case Panic::Holding:
            if (--panicCount <= 0) panic = Panic::Rising;
            break;
        case Panic::Rising:
            panicGain += panicRise;
            if (panicGain >= 1.0f) { panicGain = 1.0f; panic = Panic::Off; }
            break;
        case Panic::Off: break;
        }

        // Apply runaway fade before limiting so the detector measures the faded signal.
        for (int c = 0; c < nc; ++c) x[c] *= panicGain;

        // The true peak: the points between the samples kTpDelay and kTpDelay - 1 back, 4x interpolated (the
        // runaway detector keeps the plain peak: it judges what came in, now)
        float tpk = 0.0f;
        for (int c = 0; c < nc; ++c) {
            auto& h = tpHist[(size_t)c];
            h[(size_t)tpPos] = h[(size_t)tpPos + kTpTaps] = x[c];
            const float* w = h.data() + tpPos + 1;       // The last kTpTaps samples, oldest first
            for (int p = 0; p < 4; ++p) {
                float s = 0.0f;
                const auto& cf = tpCoef[(size_t)p];
                for (int k = 0; k < kTpTaps; ++k) s += cf[(size_t)k] * w[kTpTaps - 1 - k];
                tpk = std::max(tpk, std::fabs(s));
            }
            tpk = std::max(tpk, std::fabs(w[kTpTaps - 1 - kTpDelay]));   // And the sample itself, there
        }
        tpPos = (tpPos + 1) % kTpTaps;

        // Minimum required gain over the lookahead window, then running-average smoothing.
        // Target 0.15 dB below ceiling to allow for finite interpolation resolution.
        const float tpCeil = ceiling * 0.983f;
        const float need = tpk > tpCeil ? tpCeil / tpk : 1.0f;
        while (dqTail != dqHead && dqVal[(size_t)((dqTail - 1 + dqCap) % dqCap)] >= need)
            dqTail = (dqTail - 1 + dqCap) % dqCap;
        dqVal[(size_t)dqTail] = need;
        dqIdx[(size_t)dqTail] = tick;
        dqTail = (dqTail + 1) % dqCap;
        while (dqIdx[(size_t)dqHead] <= tick - minLen)
            dqHead = (dqHead + 1) % dqCap;
        const float wmin = dqVal[(size_t)dqHead];
        avgSum += (double)wmin - avgBuf[(size_t)avgPos];
        avgBuf[(size_t)avgPos] = wmin;
        avgPos = (avgPos + 1) % look;
        const float target = (float)(avgSum / look);
        gain = target < gain ? target : gain + (target - gain) * relCoef;    // Down at once (it is early), up slowly
        ++tick;
        minGain = std::min(minGain, gain);

        // Through the look-ahead delay: what the limiter lets out
        float lim[kMaxChannels] {};
        for (int c = 0; c < nc; ++c) {
            float& slot = delay[c][(size_t)dpos];
            lim[c] = slot * gain;
            slot = x[c];
        }
        dpos = (dpos + 1) % delayLen;

        // Measure K-weighted loudness after the limiter, before governor gain.
        // This open-loop position prevents input overload from masking gain reduction.
        // Update caps every 10 ms; release at 6 dB/s.
        {
            if (meter.push(lim, nc)) {
                const double S = meter.shortTermLufs(), M = meter.momentaryLufs(), F = meter.fastLufs();
                const double B = meter.backgroundLufs();
                double t = 0.0;
                govRate = 6.0;                           // DB/s, going down; none binding: 6
                if (S > kShortCap) { t = kShortCap - S; govRate = 2.5; }
                if (M > kMomCap && kMomCap - M < t) { t = kMomCap - M; govRate = 6.0; }
                // Compare fast loudness against background excluding the newest 300 ms.
                // During recovery, lift the transient cap at 30 dB/s.
                bool startle = false;
                if (F > kStartleFloor && F - B > kStartleJump) {
                    const double allowed = std::max(kStartleFloor, B + kStartleJump);
                    if (allowed - F < t) { t = allowed - F; govRate = 300.0; startle = true; }
                }
                govUp = startle || (govStartled && t > govDb) ? 30.0 : 6.0;
                govStartled = startle || (govStartled && t < govDb);
                govTarget = t;
            }
            const double stepDb = govRate / fs, upDb = govUp / fs;
            govDb = govTarget < govDb ? std::max(govTarget, govDb - stepDb) : std::min(govTarget, govDb + upDb);
            loudGain = (float)std::pow(10.0, govDb / 20.0);
        }

        // 8. the last clamp
        for (int c = 0; c < nc; ++c) ch[c][i] = std::clamp(lim[c] * loudGain, -ceiling, ceiling);
        if (tick > (1 << 30)) {                          // Keep the indices small; the deque holds < look
            for (int k = dqHead; k != dqTail; k = (k + 1) % dqCap)
                dqIdx[(size_t)k] -= (1 << 29);
            tick -= (1 << 29);
        }
    }
    for (int c = nc; c < numChannels; ++c)               // A third channel and more: silent, never unguarded
        std::fill(ch[c], ch[c] + n, 0.0f);
    grDb.store(-linToDb(minGain), std::memory_order_relaxed);
    loudCutDb.store(-linToDb(loudGain * panicGain), std::memory_order_relaxed);
#endif
}

} // Namespace usina
