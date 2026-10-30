// SPDX-License-Identifier: GPL-3.0-only
// Shared LPG reference scenarios for raw-output and hash comparisons.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace lpgcheck {
struct Scenario {
    const char* name;
    int mode;                                            // 1 PING, 2 EG2
    float decay, color, note;
    int strikeEvery;                                     // PING: a strike every this many samples (a block's multiple)
};
inline const Scenario kScenarios[] = {
    { "ping decay 0.5 color 0.5 C3, struck twice", 1, 0.5f, 0.5f, 48.0f, 24000 },
    { "ping decay 0.1 color 0 C5, struck often", 1, 0.1f, 0.0f, 72.0f, 6000 },
    { "ping decay 0.9 color 1 C2", 1, 0.9f, 1.0f, 36.0f, 48000 },
    { "ping decay 0 color 0.25 C7, very short", 1, 0.0f, 0.25f, 96.0f, 3000 },
    { "eg2 decay 0.6 color 0.3", 2, 0.6f, 0.3f, 60.0f, 0 },
    { "eg2 decay 1 color 0.8", 2, 1.0f, 0.8f, 60.0f, 0 },
};
constexpr int kLength = 96000;                           // (2 s at 48 kHz: a multiple of the block)

// the input: noise from a fixed seed
inline std::vector<float> input()
{
    uint32_t lcg = 1;
    std::vector<float> y((size_t)kLength);
    for (auto& v : y) {
        lcg = lcg * 1664525u + 1013904223u;
        v = (float)(int32_t)lcg * (0.5f / 2147483648.0f);
    }
    return y;
}

// EG2 for the EG2 scenarios: up over 0.2 s, held, down from 0.6 s over 0.3 s, a second note at 1.2 s
inline float eg2At(int i)
{
    const float t = (float)i / 48000.0f;
    if (t < 0.2f) return t / 0.2f;
    if (t < 0.6f) return 1.0f;
    if (t < 0.9f) return 1.0f - (t - 0.6f) / 0.3f;
    if (t < 1.2f) return 0.0f;
    if (t < 1.25f) return (t - 1.2f) / 0.05f;
    return std::exp(-(t - 1.25f) * 4.0f);
}

inline uint64_t hash(const std::vector<float>& y)
{
    uint64_t h = 1469598103934665603ull;
    const auto* p = reinterpret_cast<const unsigned char*>(y.data());
    for (size_t k = 0; k < y.size() * sizeof(float); ++k) h = (h ^ p[k]) * 1099511628211ull;
    return h;
}
} // Namespace lpgcheck
