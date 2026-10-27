// SPDX-License-Identifier: GPL-3.0-only
// Usina - the guard INSIDE a feedback loop (2026-10-20, the owner: feedback is a feature here, microphones
// included; it must not run away). Every loop path (the output fed back in, a microphone heard through the
// speakers, a send returning) passes through one before it is added back:
//   - DC block (a loop integrates DC into a slow thump),
//   - a soft ceiling (tanh around 0.7: the loop saturates musically instead of growing),
//   - a governor: an envelope follower that pulls the loop's gain under unity when the loop gets loud, so the
//     loop gain cannot stay above 1 however the user sets it (the sound breathes instead of exploding),
//   - a hard clamp at +-1.5 whatever happens.
// The OutputGuard at the end still stands behind it. Plain C++, no JUCE: the same code can go to firmware.
#pragma once
#include <algorithm>
#include <cmath>

namespace usina {

class FeedbackGuard {
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        dcR = (float)std::exp(-2.0 * 3.14159265358979 * 10.0 / fs);
        att = (float)(1.0 - std::exp(-1.0 / (0.002 * fs)));
        rel = (float)(1.0 - std::exp(-1.0 / (0.250 * fs)));
        reset();
    }
    void reset() { dcX = dcY = env = 0.0f; }

    // One sample of the loop: x the signal going back in, amount the user's loop gain (0 .. 1.5).
    float process(float x, float amount)
    {
        if (!std::isfinite(x)) { reset(); return 0.0f; }
        const float y = x - dcX + dcR * dcY;               // DC block
        dcX = x;
        dcY = y;
        const float a = std::fabs(y);
        env += (a - env) * (a > env ? att : rel);
        // governor: above 0.25 the effective loop gain falls as 0.25 / env (the product stays bounded)
        const float gov = env > 0.25f ? 0.25f / env : 1.0f;
        const float s = 0.7f * std::tanh(y * amount * gov / 0.7f);   // soft ceiling
        return std::clamp(s, -1.5f, 1.5f);
    }

private:
    double fs = 48000.0;
    float dcR = 0.9987f, dcX = 0.0f, dcY = 0.0f, env = 0.0f, att = 0.01f, rel = 0.0001f;
};

} // namespace usina
