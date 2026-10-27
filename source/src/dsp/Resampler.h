// SPDX-License-Identifier: GPL-3.0-only
// Usina - a streaming sample-rate converter between any two rates (2026-10-21: the texture processor runs at its own
// media's rate - 24, 32 or 48 kHz, as the module's codec does - and the host at its own; this stands where the codec's
// filters stand).
//
// A Kaiser-windowed sinc evaluated at each output's exact position in the input (no fixed ratio: 44.1 <-> 32 kHz as
// well as 48 <-> 24), from a table of the kernel at 512 points per input sample, read with linear interpolation. The
// cutoff at 0.46 of the LOWER rate, the kernel 32 zero crossings each side of it (beta 9). Measured (tests/
// ResamplerTest, every rate pair the texture uses): flat within 0.001 dB to 0.42 fs, aliases and images of tones
// beyond the lower rate's Nyquist under -100 dB (24 zero crossings left -0.08 dB at 0.42 fs: too soft a corner). Output m is the input's band-limited value at position m x inRate/outRate
// (no delay in the signal's time); it can be computed once the input reaches that position + halfWidth() samples -
// that wait is the converter's latency.
//
// The kernel is kept by phase (polyphase: for each of the 512 fractions of an input sample, its taps in a row - an
// output's taps all share one fraction, so its kernel is two rows, read in order, the fraction between them the
// interpolation's), and the history twice over (each input written at w and w + len, so a kernel's span is read in one
// run, without the ring's wrap): measured 22/10 (tests/CpuBench, the MACRO operator, 8 voices converting, over a plain
// FM operator) 6.5 % of a core -> about 3.5 %, against evaluating each tap's position - the same kernel, the same
// measured response (tests/ResamplerTest).
//
// Streaming: push() the input as it comes, pull() outputs while available() says there are; the read position advances
// by inRate / outRate input samples per output, in double precision (no drift over hours). Stereo, interleaved
// state per channel. No allocation after prepare().
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace usina {

class Resampler {
public:
    static constexpr int kChannels = 2;
    static constexpr int kPhases = 512;                  // kernel points per input sample
    static constexpr double kZeroCrossings = 32.0;
    static constexpr double kBeta = 9.0;
    static constexpr double kCutoff = 0.46;              // of the lower rate

    // inRate -> outRate; maxPush: the most input samples pushed at once (the history ring holds them + the kernel)
    void prepare(double inRate, double outRate, int maxPush)
    {
        in = inRate;
        out = outRate;
        step = inRate / outRate;
        const double cutoffHz = kCutoff * std::min(inRate, outRate);
        k = 2.0 * cutoffHz / inRate;                     // the cutoff as a fraction of the input's Nyquist
        half = kZeroCrossings / k;                       // the kernel's half-width, in input samples
        // rows 0..kPhases + 1 (the interpolation reads row q + 1; at a fraction of exactly 0 the right side's distance
        // is a whole sample: row kPhases), each taps 0..taps-1 at distance q / kPhases + m input samples
        taps = (int)std::floor(half) + 2;
        poly.assign((size_t)((kPhases + 2) * taps), 0.0f);
        const double i0b = besselI0(kBeta);
        for (int q = 0; q < kPhases + 2; ++q) {
            for (int m = 0; m < taps; ++m) {
                const double t = (double)q / kPhases + m; // (input samples from the centre)
                if (t >= half) break;
                const double x = k * t;
                const double sinc = x == 0.0 ? 1.0 : std::sin(kPi * x) / (kPi * x);
                const double r = t / half;
                const double w = besselI0(kBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
                poly[(size_t)(q * taps + m)] = (float)(k * sinc * w);
            }
        }
        len = 1;
        while (len < maxPush + (int)std::ceil(2.0 * half) + 8) len <<= 1;
        for (auto& h : hist) h.assign((size_t)(2 * len), 0.0f);
        mask = len - 1;
        reset();
    }

    // emptied; the first output at input position start (negative: before the first input, which is silence there -
    // a fractional start makes a whole-sample delay across two converters of non-integer ratio)
    void reset(double start = 0.0)
    {
        for (auto& h : hist) std::fill(h.begin(), h.end(), 0.0f);
        written = 0;
        pos = start;
    }

    double halfWidth() const { return half; }            // the delay, in input samples
    double ratio() const { return step; }                // input samples per output

    void push(const float* l, const float* r, int n)
    {
        for (int i = 0; i < n; ++i) {
            const size_t w = (size_t)((written + i) & mask);
            hist[0][w] = hist[0][w + (size_t)len] = l[i];
            hist[1][w] = hist[1][w + (size_t)len] = r[i];
        }
        written += n;
    }

    // how many outputs the input pushed so far allows (each needs the input up to its position + the half-width)
    int available() const
    {
        const double last = (double)written - 1.0 - half; // the furthest centre the input covers
        if (last < pos) return 0;
        return (int)std::floor((last - pos) / step) + 1;
    }

    void pull(float* l, float* r, int n)
    {
        const float* h0 = hist[0].data();
        const float* h1 = hist[1].data();
        const int64_t jmin = std::max<int64_t>(0, written - (int64_t)mask);   // (the oldest input still held)
        for (int i = 0; i < n; ++i) {
            const double p = pos;                        // (the output's position in the input: its kernel's centre)
            const double fl = std::floor(p);
            const int64_t c = (int64_t)fl;               // (the input at or before it)
            const double f = p - fl;
            float al = 0.0f, ar = 0.0f;
            // the left side: inputs c - m, at distance f + m (all within the half-width, none older than held)
            {
                const double t = f * kPhases;
                const int q = (int)t;
                const float fr = (float)(t - q);
                const int64_t nl = std::min<int64_t>((int64_t)std::floor(half - f) + 1, c - jmin + 1);
                const float* ra = poly.data() + (size_t)(q * taps);
                const float* rb = ra + taps;
                const float* x0 = h0 + (size_t)(c & mask) + (size_t)len;   // (the upper copy: x0 - m stays inside)
                const float* x1 = h1 + (size_t)(c & mask) + (size_t)len;
                for (int m = 0; m < (int)nl; ++m) {
                    const float h = ra[m] + fr * (rb[m] - ra[m]);
                    al += h * x0[-m];
                    ar += h * x1[-m];
                }
            }
            // the right side: inputs c + 1 + m, at distance 1 - f + m (none before the oldest held)
            {
                const double t = (1.0 - f) * kPhases;
                const int q = (int)t;
                const float fr = (float)(t - q);
                const int nr = (int)std::floor(half - (1.0 - f)) + 1;
                const int m0 = (int)std::clamp<int64_t>(jmin - (c + 1), 0, nr);
                const float* ra = poly.data() + (size_t)(q * taps);
                const float* rb = ra + taps;
                const float* x0 = h0 + (size_t)((c + 1 + m0) & mask) - m0;     // (indexed by m: x0[m])
                const float* x1 = h1 + (size_t)((c + 1 + m0) & mask) - m0;
                for (int m = m0; m < nr; ++m) {
                    const float h = ra[m] + fr * (rb[m] - ra[m]);
                    al += h * x0[m];
                    ar += h * x1[m];
                }
            }
            l[i] = al;
            r[i] = ar;
            pos += step;
        }
        // (keep the position's integer part small: rebase both counters together, exactly)
        if (pos > 1.0e9) {
            const int64_t shift = (int64_t)1 << 29;
            pos -= (double)shift;
            written -= shift;
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static double besselI0(double x)
    {
        double sum = 1.0, term = 1.0;
        for (int i = 1; i < 64; ++i) {
            term *= (x / (2.0 * i)) * (x / (2.0 * i));
            sum += term;
            if (term < sum * 1e-17) break;
        }
        return sum;
    }

    double in = 48000.0, out = 48000.0, step = 1.0, k = 1.0, half = 1.0;
    std::vector<float> poly;                            // (the kernel by phase: rows of taps)
    std::vector<float> hist[kChannels];                  // (each input twice: at w and w + len)
    int taps = 1;
    int64_t len = 1, mask = 0, written = 0;
    double pos = 0.0;
};

} // namespace usina
