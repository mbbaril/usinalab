// SPDX-License-Identifier: GPL-3.0-only
// Feedback-path guard: DC blocker, soft saturation, gain governor and final clamp.
// Above envelope 0.25, effective gain scales by 0.25/env.
// Soft saturation is centered around 0.7; final clamp is +/-1.5.
// This precedes feedback summation; OutputGuard remains downstream.
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

    // Process one return sample with requested loop gain in 0..1.5.
    float process(float x, float amount)
    {
        if (!std::isfinite(x)) { reset(); return 0.0f; }
        const float y = x - dcX + dcR * dcY;               // DC block
        dcX = x;
        dcY = y;
        const float a = std::fabs(y);
        env += (a - env) * (a > env ? att : rel);
        // Governor: above 0.25 the effective loop gain falls as 0.25 / env (the product stays bounded)
        const float gov = env > 0.25f ? 0.25f / env : 1.0f;
        const float s = 0.7f * std::tanh(y * amount * gov / 0.7f);   // Soft ceiling
        return std::clamp(s, -1.5f, 1.5f);
    }

private:
    double fs = 48000.0;
    float dcR = 0.9987f, dcX = 0.0f, dcY = 0.0f, env = 0.0f, att = 0.01f, rel = 0.0001f;
};

} // Namespace usina
