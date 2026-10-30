// SPDX-License-Identifier: GPL-3.0-only
// Stereo integer-sample delay for bus dry/wet alignment.
// Crossfade tap changes and feed every block to keep history available.
// No allocation after prepare().
#pragma once
#include <algorithm>
#include <vector>

namespace usina {

class AlignDelay {
public:
    // Maximum delay and transition crossfade length, in samples.
    void prepare(int maxDelay, int fade)
    {
        int len = 1;
        while (len < maxDelay + 2) len <<= 1;
        bl.assign((size_t)len, 0.0f);
        br.assign((size_t)len, 0.0f);
        mask = len - 1;
        maxD = maxDelay;
        fadeLen = std::max(1, fade);
        reset();
    }
    void reset()
    {
        std::fill(bl.begin(), bl.end(), 0.0f);
        std::fill(br.begin(), br.end(), 0.0f);
        w = 0;
        cur = prev = 0;
        fadeLeft = 0;
    }
    int delay() const { return cur; }

    // Delay stereo audio in place, crossfading from the previous tap on changes.
    void process(float* l, float* r, int n, int delay)
    {
        delay = std::clamp(delay, 0, maxD);
        if (delay != cur) {
            prev = fadeLeft > 0 ? prev : cur;            // A change mid-fade: from where the fade started
            cur = delay;
            fadeLeft = fadeLen;
        }
        for (int i = 0; i < n; ++i) {
            bl[(size_t)w] = l[i];
            br[(size_t)w] = r[i];
            const int a = (w - cur) & mask;
            float ol = bl[(size_t)a], orr = br[(size_t)a];
            if (fadeLeft > 0) {
                const int b = (w - prev) & mask;
                const float t = (float)fadeLeft / (float)fadeLen;   // 1 -> 0: the old tap's share
                ol += (bl[(size_t)b] - ol) * t;
                orr += (br[(size_t)b] - orr) * t;
                --fadeLeft;
            }
            l[i] = ol;
            r[i] = orr;
            w = (w + 1) & mask;
        }
    }

private:
    std::vector<float> bl, br;
    int mask = 0, w = 0, maxD = 0, cur = 0, prev = 0, fadeLeft = 0, fadeLen = 1;
};

} // Namespace usina
