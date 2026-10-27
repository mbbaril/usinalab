// SPDX-License-Identifier: GPL-3.0-only
// Usina - the howl guard (docs/SEGURANCA.md, item 4): the howl detector listening to the path from outside the synth
// (the audio input and the feedback loop's return), and up to four narrow notches on that same path, one at each
// howl it finds - the classic adaptive-notch answer to acoustic feedback. A notch comes in over ~30 ms (-30 dB,
// Q 15: a sixth of a tone wide), stays while its howl is heard and 10 s after, then goes out over 0.5 s. The detector
// listens after the notches: a howl dealt with is gone from it, a new one at another pitch takes the next notch.
// The synth's own voices never pass here (a held note is not a howl). Plain C++, no allocation after prepare().
#pragma once
#include "HowlDetector.h"
#include <array>
#include <atomic>
#include <cmath>

namespace usina {

class HowlGuard {
public:
    static constexpr int kNotches = 4;

    void prepare(double sampleRate)
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        det.prepare(fs);
        reset();
    }

    void reset()
    {
        det.reset();
        for (auto& n : notch) n = Notch {};
        sub = 0;
        found.store(0.0f, std::memory_order_relaxed);
        active.store(0, std::memory_order_relaxed);
    }

    // one sample of the outside path, both channels, in place
    void process(float& l, float& r)
    {
        if (++sub >= kSub) {                            // the notches' gains move, their coefficients follow
            sub = 0;
            int on = 0;
            for (auto& n : notch) {
                n.update(fs, kSub);
                on += n.depthDb < -0.01 ? 1 : 0;
            }
            active.store(on, std::memory_order_relaxed);
        }
        double a = l, b = r;
        for (auto& n : notch)
            if (n.depthDb < -0.01) { a = n.step(0, a); b = n.step(1, b); }
        l = (float)a;
        r = (float)b;
        if (det.push(0.5f * (l + r)) && det.howling()) take(det.frequency());
    }

    // for the interface (any thread): the last howl found while a notch is on (Hz), else 0; how many notches are on
    float notchedHz() const { return active.load(std::memory_order_relaxed) > 0 ? found.load(std::memory_order_relaxed) : 0.0f; }
    int activeNotches() const { return active.load(std::memory_order_relaxed); }

private:
    static constexpr int kSub = 32;                     // (samples between the notches' coefficient updates)
    struct Notch {
        double hz = 0.0, depthDb = 0.0;                  // where; how deep now (0 .. -30 dB)
        double hold = 0.0;                               // seconds left at full depth
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
        void update(double fs, int n)
        {
            const double dt = n / fs;
            if (hold > 0.0) {
                hold -= dt;
                depthDb = std::max(-30.0, depthDb - 30.0 * dt / 0.03);   // in over ~30 ms
            } else if (depthDb < 0.0) {
                depthDb = std::min(0.0, depthDb + 30.0 * dt / 0.5);      // out over 0.5 s
            }
            if (depthDb >= -0.01) { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; return; }
            // a peaking EQ at hz, Q 15, depthDb (RBJ): at full depth a notch
            constexpr double kPi = 3.14159265358979323846, q = 15.0;
            const double A = std::pow(10.0, depthDb / 40.0), w = 2.0 * kPi * hz / fs, al = std::sin(w) / (2.0 * q);
            const double a0 = 1.0 + al / A;
            b0 = (1.0 + al * A) / a0;
            b1 = -2.0 * std::cos(w) / a0;
            b2 = (1.0 - al * A) / a0;
            a1 = b1;
            a2 = (1.0 - al / A) / a0;
        }
        double step(int c, double x)
        {
            const double y = b0 * x + z1[c];
            z1[c] = b1 * x - a1 * y + z2[c];
            z2[c] = b2 * x - a2 * y;
            return std::isfinite(y) ? y : (z1[c] = z2[c] = 0.0);
        }
    };

    // a howl at hz: the notch already on it (within 3 %) is held longer; else a free one, or the one closest to its end
    void take(double hz)
    {
        Notch* best = nullptr;
        for (auto& n : notch)
            if (n.depthDb < -0.01 && std::fabs(n.hz - hz) < 0.03 * hz) best = &n;
        if (best == nullptr)
            for (auto& n : notch)
                if (best == nullptr || (n.depthDb >= -0.01 && best->depthDb < -0.01) ||
                    ((n.depthDb < -0.01) == (best->depthDb < -0.01) && n.hold < best->hold))
                    best = &n;
        if (best->depthDb >= -0.01 || std::fabs(best->hz - hz) >= 0.03 * hz) {
            best->hz = hz;
            best->depthDb = std::min(best->depthDb, -0.02);   // (on from now; its states from rest)
            best->z1[0] = best->z1[1] = best->z2[0] = best->z2[1] = 0.0;
        }
        best->hold = 10.0;
        found.store((float)hz, std::memory_order_relaxed);
    }

    double fs = 48000.0;
    HowlDetector det;
    std::array<Notch, kNotches> notch {};
    int sub = 0;
    std::atomic<float> found { 0.0f };
    std::atomic<int> active { 0 };
};

} // namespace usina
