// SPDX-License-Identifier: GPL-3.0-only
// Persistent narrowband feedback detector for external input and loop returns.
// Uses PAPR, PNPR, PHPR, IPMP and level-growth criteria; see van Waterschoot
// and Moonen, "Fifty years of acoustic feedback control", Proc. IEEE 2011.
// Analyze 2048-sample Hann frames with 1024-sample hops.
// Exclude synthesizer voices from this path. No allocation after prepare().
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace usina {

class HowlDetector {
public:
    static constexpr int kN = 2048, kHop = 1024, kHistory = 16;   // 16 hops: ~340 ms at 48 kHz

    void prepare(double sampleRate)
    {
        constexpr double kPi = 3.14159265358979323846;
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        // At higher rates the frame is decimated by the rate's ratio, so a bin is ~23 Hz at any rate
        step = std::max(1, (int)std::lround(fs / 48000.0));
        buf.assign(kN, 0.0f);
        spec.assign(kN, {});
        win.resize(kN);
        for (int i = 0; i < kN; ++i) win[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * i / kN));
        tw.resize(kN / 2);
        for (int i = 0; i < kN / 2; ++i) tw[(size_t)i] = std::polar(1.0, -2.0 * kPi * i / kN);
        reset();
    }

    void reset()
    {
        std::fill(buf.begin(), buf.end(), 0.0f);
        pos = sinceHop = dec = 0;
        decAcc = 0.0;
        peakBins.fill(-1);
        peakDb.fill(-200.0);
        frames = 0;
        found = false;
        foundHz = 0.0;
    }

    // One sample of the outside path; true when a frame was analysed (then howling() and frequency() are new)
    bool push(float x)
    {
        decAcc += x;                                     // A plain average over 'step' samples: enough for < 10 kHz
        if (++dec < step) return false;
        buf[(size_t)pos] = (float)(decAcc / step);
        decAcc = 0.0;
        dec = 0;
        pos = (pos + 1) % kN;
        if (++sinceHop < kHop) return false;
        sinceHop = 0;
        analyse();
        return true;
    }

    bool howling() const { return found; }
    double frequency() const { return foundHz; }         // Hz, while howling

private:
    void analyse()
    {
        // The frame, oldest first, windowed; its FFT (radix 2, in place)
        for (int i = 0; i < kN; ++i) spec[(size_t)i] = { (double)buf[(size_t)((pos + i) % kN)] * win[(size_t)i], 0.0 };
        fft();
        const double binHz = fs / step / kN;
        const int lo = std::max(2, (int)(100.0 / binHz)), hi = std::min(kN / 2 - 2, (int)(10000.0 / binHz));
        std::array<double, kN / 2> p {};
        int k = lo;
        for (int i = lo; i <= hi; ++i) {
            p[(size_t)i] = std::norm(spec[(size_t)i]) + 1e-20;
            if (p[(size_t)i] > p[(size_t)k]) k = i;
        }
        // Use median band power as the noise floor; the mean is biased by the candidate peak.
        std::copy(p.begin() + lo, p.begin() + hi + 1, sorted.begin());
        const int half = (hi - lo + 1) / 2;
        std::nth_element(sorted.begin(), sorted.begin() + half, sorted.begin() + (hi - lo + 1));
        const double mean = sorted[(size_t)half];
        const auto db = [](double v) { return 10.0 * std::log10(v); };
        // A Hann window spreads a sine over 3 bins: the peak is their sum; its neighbours, 4..8 bins away
        const double peak = p[(size_t)k] + p[(size_t)(k - 1)] + p[(size_t)(k + 1)];
        double nb = 0.0;
        for (int d = 4; d <= 8; ++d) nb = std::max({ nb, k - d >= lo ? p[(size_t)(k - d)] : 0.0, k + d <= hi ? p[(size_t)(k + d)] : 0.0 });
        double harm = 0.0;                               // The 2nd, 3rd and the half, each its 3 bins' sum
        for (const double r : { 2.0, 3.0, 0.5 }) {
            const int h = (int)std::lround(k * r);
            if (h - 1 >= lo && h + 1 <= hi) harm = std::max(harm, p[(size_t)(h - 1)] + p[(size_t)h] + p[(size_t)(h + 1)]);
        }
        const double papr = db(peak / 3.0) - db(mean), pnpr = db(peak / 3.0) - db(nb + 1e-20);
        const double phpr = harm > 0.0 ? db(peak) - db(harm) : 60.0;
        // The peak's level in dBFS: a full-scale sine sums to (kN/4)^2 x 1.5 over its 3 bins (Hann)
        const double level = db(peak / (1.5 * (kN / 4.0) * (kN / 4.0)));
        const bool candidate = papr > 30.0 && pnpr > 15.0 && phpr > 20.0 && level > -50.0;
        // The history: the candidate's bin (or -1) and level, frame by frame
        for (int i = kHistory - 1; i > 0; --i) { peakBins[(size_t)i] = peakBins[(size_t)i - 1]; peakDb[(size_t)i] = peakDb[(size_t)i - 1]; }
        peakBins[0] = candidate ? k : -1;
        peakDb[0] = candidate ? level : -200.0;
        frames = std::min(frames + 1, kHistory);
        // IPMP: the same bin (+-1) in 12 of the last 16 frames; and growing 3 dB over them, or already above -12 dBFS
        int same = 0;
        for (int i = 0; i < frames; ++i) same += peakBins[(size_t)i] >= 0 && std::abs(peakBins[(size_t)i] - k) <= 1 ? 1 : 0;
        const double oldest = peakDb[(size_t)(frames - 1)];
        const bool rising = oldest > -199.0 && level - oldest > 3.0;
        found = candidate && frames == kHistory && same >= 12 && (rising || level > -12.0);
        if (found) foundHz = k * binHz;
    }

    void fft()
    {
        for (int i = 1, j = 0; i < kN; ++i) {           // Bit reversal
            int bit = kN >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(spec[(size_t)i], spec[(size_t)j]);
        }
        for (int len = 2; len <= kN; len <<= 1) {
            const int stride = kN / len;
            for (int i = 0; i < kN; i += len)
                for (int j = 0; j < len / 2; ++j) {
                    const std::complex<double> u = spec[(size_t)(i + j)], v = spec[(size_t)(i + j + len / 2)] * tw[(size_t)(j * stride)];
                    spec[(size_t)(i + j)] = u + v;
                    spec[(size_t)(i + j + len / 2)] = u - v;
                }
        }
    }

    double fs = 48000.0;
    int step = 1, dec = 0, pos = 0, sinceHop = 0, frames = 0;
    double decAcc = 0.0;
    std::vector<float> buf, win;
    std::vector<std::complex<double>> spec, tw;
    std::array<double, kN / 2> sorted {};               // The band's powers, for the median
    std::array<int, kHistory> peakBins {};
    std::array<double, kHistory> peakDb {};
    bool found = false;
    double foundHz = 0.0;
};

} // Namespace usina
