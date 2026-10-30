// SPDX-License-Identifier: GPL-3.0-only
// Match dry-path latency, DC blocking and oversampling phase to the active effects.
// Linear stages commute; keep their state warm and crossfade changes over 20 ms.
// Bypass preserves the dry samples. No allocation after prepare().
#pragma once
#include "dsp/AlignDelay.h"
#include "fx/Distort.h"
#include <array>
#include <cmath>

namespace usina::fx {

class BusMatch {
public:
    void prepare(double fs, int maxDelay)
    {
        delay.prepare(maxDelay, std::max(1, (int)std::lround(0.020 * fs)));
        step = (float)(1.0 / (0.020 * fs));
        pole = Distort::dcPoleFor(fs);
        reset();
    }
    void reset()
    {
        delay.reset();
        for (auto& t : trip) t.reset();
        for (auto& d : dc) d.reset();
        dcShare = tripShare = 0.0f;
        cold = true;
    }

    // Process stereo dry audio in place. latency is in samples.
    // When warm, run inactive stages to preserve state for effect transitions.
    void process(float* l, float* r, int n, int latency, bool distort, bool oversampled, bool warm)
    {
        const float dcTo = distort ? 1.0f : 0.0f, tripTo = distort && oversampled ? 1.0f : 0.0f;
        const bool needed = dcTo > 0.0f || tripTo > 0.0f || dcShare > 0.0f || tripShare > 0.0f;
        if (!needed && !warm) {
            cold = true;
            delay.process(l, r, n, latency);
            return;
        }
        if (cold) {
            for (auto& t : trip) t.reset();
            for (auto& d : dc) d.reset();
            cold = false;
        }
        if (needed) {
            float* ch[2] = { l, r };
            for (int i = 0; i < n; ++i) {
                dcShare = glide(dcShare, dcTo);
                tripShare = glide(tripShare, tripTo);
                for (int c = 0; c < 2; ++c) {
                    const double x = ch[c][i];
                    const double d = x + (dc[(size_t)c].process(x, pole) - x) * dcShare;
                    ch[c][i] = (float)(d + (trip[(size_t)c].process(d) - d) * tripShare);
                }
            }
        } else {
            for (int i = 0; i < n; ++i)                  // Keep stage state warm without changing the bypassed dry samples.
                for (int c = 0; c < 2; ++c) {
                    const double x = c ? r[i] : l[i];
                    trip[(size_t)c].process(dc[(size_t)c].process(x, pole));
                }
        }
        delay.process(l, r, n, latency);
    }

private:
    float glide(float v, float to) const { return to > v ? std::min(to, v + step) : std::max(to, v - step); }
    usina::AlignDelay delay;
    std::array<Distort::RoundTrip, 2> trip {};
    std::array<Distort::DcBlock, 2> dc {};
    double pole = 0.999;
    float dcShare = 0.0f, tripShare = 0.0f, step = 0.001f;
    bool cold = true;
};

} // Namespace usina::fx
