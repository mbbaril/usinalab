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

#include "Svf.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ruidocore {

// A faithful port of Mutable Instruments' Plaits `Particle`
// (plaits/dsp/noise/particle.h, "Random impulse train processed by a
// resonant filter", MIT licensed, github.com/pichenettes/eurorack,
// downloaded and read directly) - a stochastic impulse generator
// (probability `density` per sample) whose filter frequency is
// re-randomized (within `spread` semitones of `frequency`) each time a
// new impulse fires, held fixed for the rest of that impulse's own
// filter ring - one of `kNumParticles` running in parallel is what
// gives ParticleEngine its "cloud of resonant clicks" character.
class Particle {
public:
    // Own xorshift32 PRNG - statistical/spectral character is what
    // matters here, not bit-exact match to Plaits' own generator (same
    // reasoning as AnalogSnareDrum/AnalogHiHat/ClockedNoise).
    float nextRandom01() {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return static_cast<float>(rngState) / 4294967295.0f;
    }

    void render(bool sync, float density, float gain, float frequency, float spread, float q, float* out, float* aux,
                size_t size) {
        float u = nextRandom01();
        if (sync)
            u = density;

        bool canRandomizeFrequency = true;
        while (size--) {
            float s = 0.0f;
            if (u <= density) {
                s = u * gain;
                if (canRandomizeFrequency) {
                    const float spreadRoll = 2.0f * nextRandom01() - 1.0f;
                    const float f = std::min(std::exp2(spread * spreadRoll / 12.0f) * frequency, 0.25f);
                    preGain = 0.5f / std::sqrt(q * f * std::sqrt(std::max(density, 1e-9f)));
                    filter.setFrequencyAndResonance(f, q);
                    canRandomizeFrequency = false; // cutoff held fixed for the rest of this impulse's own ring
                }
            }
            *aux++ += s;
            *out++ += filter.process(preGain * s, Svf::Mode::BandPass);
            u = nextRandom01();
        }
    }

private:
    float preGain = 0.0f;
    Svf filter;
    std::uint32_t rngState = 0x85ebca6bu;
};

} // namespace ruidocore
