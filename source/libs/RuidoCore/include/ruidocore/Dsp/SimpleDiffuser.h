// ---------------------------------------------------------------------------
// This file ports code from Mutable Instruments' Eurorack firmware
// (https://github.com/pichenettes/eurorack), used under the MIT licence.
// The upstream file it derives from is named in the comment below.
//
//   Copyright 2016 Emilie Gillet.
//
//   Permission is hereby granted, free of charge, to any person obtaining a
//   copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to
//   permit persons to whom the Software is furnished to do so, subject to
//   the following conditions:
//
//   The above copyright notice and this permission notice shall be included
//   in all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// RuidoCore as a whole, including this port, is licensed under the AGPLv3 -
// see LICENSE at the repository root. See also THIRD_PARTY.md.
// ---------------------------------------------------------------------------
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace ruidocore {

// A DISCLOSED SIMPLIFICATION, not a faithful port, of Mutable
// Instruments' Plaits `Diffuser` (plaits/dsp/fx/diffuser.h) - real
// Plaits' version runs its own custom delay-line DSL (`FxEngine`, a
// whole macro-based system for chained/LFO-modulated/12-bit-compressed
// delay taps) across a 7-tap allpass network with an LFO-modulated long
// delay. Porting that DSL faithfully is a disproportionate undertaking
// for the one knob (Morph past its midpoint, in ParticleEngine) that
// uses it, and it doesn't define this engine's actual percussive
// character the way Particle.h's own resonant-impulse mechanism does.
//
// This is a plain, un-modulated 4-stage fixed-delay allpass chain
// (Schroeder-style) - same STRUCTURAL idea (several short allpasses
// smear a transient into a denser, less clicky texture) without
// chasing the real DSL's exact taps/gains/LFO. Kept as its own small
// class specifically so it's never mistaken for a verified port.
class SimpleDiffuser {
public:
    void reset() {
        for (auto& line : lines)
            line.fill(0.0f);
        for (auto& p : writePos)
            p = 0;
    }

    // amount: 0..1 (dry/wet). `rt` (real Plaits' own long-delay feedback
    // amount) is unused here - no long delay line in this simplified
    // version, just the short allpass smear.
    void process(float amount, float /*rt*/, float* inOut, size_t size) {
        while (size--) {
            float s = *inOut;
            for (int stage = 0; stage < kStages; ++stage) {
                auto& line = lines[static_cast<size_t>(stage)];
                auto& pos = writePos[static_cast<size_t>(stage)];
                const int length = kLengths[stage];
                const float delayed = line[static_cast<size_t>(pos)];
                const float w = s + delayed * -kApCoefficient;
                line[static_cast<size_t>(pos)] = w;
                s = delayed + w * kApCoefficient;
                pos = (pos + 1) % length;
            }
            *inOut += amount * (s - *inOut);
            ++inOut;
        }
    }

private:
    static constexpr int kStages = 4;
    static constexpr float kApCoefficient = 0.625f;
    static constexpr int kLengths[kStages] = {126, 180, 269, 444}; // real Plaits' own first 4 allpass tap lengths, reused here even though the surrounding DSL isn't

    std::array<std::array<float, 444>, kStages> lines{};
    std::array<int, kStages> writePos{};
};

} // namespace ruidocore
