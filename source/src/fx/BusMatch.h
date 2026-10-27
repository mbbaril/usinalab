// SPDX-License-Identifier: GPL-3.0-only
// Usina - a bus's dry made to match what its effects do to the signal they pass (22/10), so a bus MIX below 100 %
// blends two things in phase:
//   - held back as long as the effects hold theirs (the texture, VAPOR: behind their converters - dsp/AlignDelay.h);
//     without it, VAPOR at a MIX of 50 % left a residual of 0.82 against the voices late (a comb)
//   - DISTORT's linear path: its DC blocker (every mode: fx/Distort.h DcBlock) and, in the oversampled modes, its
//     oversampling's round trip (an all-pass: Distort::RoundTrip); without them, a MIX of 50 % dipped 12 dB at 5 kHz
//     and 20 dB at 18 kHz, and lost 3.6 % of a bass-heavy voice
// All of them linear: they commute, so one of each stands for a bus. Each stage runs always (its state warm) and is
// faded in and out (20 ms) as DISTORT comes, goes or changes mode; a change of the delay is crossfaded by the delay
// itself. With nothing to match, the dry passes bit for bit. No allocation after prepare().
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

    // in place: l / r as the effects leave a dry - `latency` samples late; with DISTORT running, through its DC
    // blocker, and through its round trip when its mode is oversampled. warm: DISTORT is in a running bus (it may come
    // here): the stages run even when not needed; else they rest, and start from rest (faded in) when needed
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
            for (int i = 0; i < n; ++i)                  // (nothing to match: the stages kept warm, the dry untouched)
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

} // namespace usina::fx
