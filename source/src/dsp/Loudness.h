// SPDX-License-Identifier: GPL-3.0-only
// Usina - loudness as ITU-R BS.1770 measures it: the K-weighting (a high shelf, +4 dB above ~1.7 kHz, where the ear
// is most sensitive; a high-pass at ~38 Hz, where it is least), the channels' mean squares summed, -0.691 + 10 log10.
// Three windows, slid every 10 ms over 10 ms sub-blocks: FAST 50 ms (a startle), MOMENTARY 400 ms and SHORT-TERM
// 3 s (BS.1770 / EBU R128's own). The filters' analogue parameters are the standard's 48 kHz coefficients' (as
// derived for any rate in the open literature: f0, gain and Q), so any host rate measures the same.
// Plain C++, no allocation after prepare(); the guard and the tests share it.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace usina {

class Loudness {
public:
    static constexpr int kMaxChannels = 2;

    void prepare(double fs)
    {
        constexpr double kPi = 3.14159265358979323846;
        // stage 1: the high shelf (f0 1681.97 Hz, +4.0 dB, Q 0.7072); stage 2: the high-pass (f0 38.135 Hz, Q 0.5003)
        {
            const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
            const double k = std::tan(kPi * f0 / fs), vh = std::pow(10.0, g / 20.0), vb = std::pow(vh, 0.4996667741545416);
            const double a0 = 1.0 + k / q + k * k;
            shelf = { (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                      2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        }
        {
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            const double k = std::tan(kPi * f0 / fs), a0 = 1.0 + k / q + k * k;
            high = { 1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        }
        sub = std::max(1, (int)std::lround(fs * 0.010));
        ring.assign(300, 0.0);
        reset();
    }

    void reset()
    {
        for (auto& s : state) s = {};
        std::fill(ring.begin(), ring.end(), 0.0);
        pos = 0;
        count = 0;
        acc = 0.0;
        filled = 0;
        fast = mom = shortTerm = background = -200.0;
    }

    // one sample of each channel (K-weighted here); every 10 ms the three windows move. Returns true when they did
    bool push(const float* x, int numChannels)
    {
        double e = 0.0;
        for (int c = 0; c < std::min(numChannels, kMaxChannels); ++c) {
            const double y = state[(size_t)c].step(shelf, high, x[c]);
            e += y * y;
        }
        acc += e;
        if (++count < sub) return false;
        ring[(size_t)pos] = acc / sub;                   // (the sub-block's mean square, channels summed)
        pos = (pos + 1) % (int)ring.size();
        filled = std::min(filled + 1, (int)ring.size());
        acc = 0.0;
        count = 0;
        fast = lufs(5);
        mom = lufs(40);
        shortTerm = lufs(300);
        background = lufs(300, 30);
        return true;
    }

    // the windows' loudness now (LUFS; -200 for silence)
    double fastLufs() const { return fast; }
    double momentaryLufs() const { return mom; }
    double shortTermLufs() const { return shortTerm; }
    // the BACKGROUND: the 3 s before the last 300 ms (what was there before a sound that just began: a jump is
    // measured against it, never against a window the jump itself already fills)
    double backgroundLufs() const { return background; }

private:
    struct Coef { double b0, b1, b2, a1, a2; };
    struct Stage {
        double s1 = 0, s2 = 0, h1 = 0, h2 = 0;           // (the shelf's, the high-pass's: transposed direct form II)
        double step(const Coef& a, const Coef& b, double x)
        {
            const double y = a.b0 * x + s1;
            s1 = a.b1 * x - a.a1 * y + s2;
            s2 = a.b2 * x - a.a2 * y;
            const double z = b.b0 * y + h1;
            h1 = b.b1 * y - b.a1 * z + h2;
            h2 = b.b2 * y - b.a2 * z;
            return std::isfinite(z) ? z : (s1 = s2 = h1 = h2 = 0.0);
        }
    };
    // the last 'blocks' sub-blocks, the newest 'skip' left out
    double lufs(int blocks, int skip = 0) const
    {
        const int n = std::min(blocks, std::max(1, filled));
        double s = 0.0;
        for (int i = 1 + skip; i <= n; ++i) s += ring[(size_t)((pos - i + (int)ring.size()) % (int)ring.size())];
        s /= (blocks - skip);                            // (a window not yet full counts its missing part as silence)
        return s > 1e-20 ? -0.691 + 10.0 * std::log10(s) : -200.0;
    }

    Coef shelf {}, high {};
    std::array<Stage, kMaxChannels> state {};
    std::vector<double> ring;
    int sub = 480, pos = 0, count = 0, filled = 0;
    double acc = 0.0, fast = -200.0, mom = -200.0, shortTerm = -200.0, background = -200.0;
};

} // namespace usina
