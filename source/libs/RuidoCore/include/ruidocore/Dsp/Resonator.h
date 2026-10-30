// ---------------------------------------------------------------------------
// Ported from MIT-licensed code: the notice below. Provenance: THIRD_PARTY.md.
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

#include "ResonatorTables.h"
#include "Svf.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ruidocore {

// Resonator DSP implementation.
// See THIRD_PARTY.md and the preserved source-license notice for attribution.
//
// 24 resonant band-pass modes (`kMaxNumModes`), each an independent
// `Svf` instance - reference batches them 4-at-a-time
// (`ResonatorSvf<4>`) purely as a register-allocation trick for its own
// embedded target; this port uses 24 separate Svf instances instead,
// which is mathematically IDENTICAL (same ZDF/TPT coefficients, g=tan
// (f), r=1/q, h=1/(1+r*g+g*g) - confirmed by reading the real
// ResonatorSvf::Process code directly), just not SIMD-batched - a pure
// implementation-detail difference, not an algorithm simplification.
//
// Mode frequencies use accumulated stiffness stretch and harmonic compensation.
// Excitation position is 0.015; mode taper is 0.5*cos(2*pi*i*position)+0.5.
class Resonator {
public:
    static constexpr int kMaxNumModes = 24;

    void init(float position, int resolution) {
        resolution_ = std::min(resolution, kMaxNumModes);
        for (int i = 0; i < resolution_; ++i)
            modeAmplitude[static_cast<size_t>(i)] =
                (0.5f * std::cos(2.0f * kPi * static_cast<float>(i) * position) + 0.5f) * 0.25f;
        for (auto& f : modeFilters)
            f.reset();
    }

    // structure: inharmonicity/material (0..1). brightness: excitation
    // brightness parameter, ALSO reused here to shape mode Q loss (real
    // reference dual-use of this parameter, not a mistake - see the
    // `brightness *= ...` lines below). damping: energy-absorption/decay
    // parameter (0..1).
    void process(float f0, float structure, float brightness, float damping, const float* in, float* out,
                 size_t size) {
        float stiffness = interpolate(kStiffnessTable, structure, 64.0f);
        f0 *= nthHarmonicCompensation(3, stiffness);

        float harmonic = f0;
        float stretchFactor = 1.0f;
        const float qSqrt = semitonesToRatio(damping * 79.7f);
        float q = 500.0f * qSqrt * qSqrt;
        brightness *= 1.0f - structure * 0.3f;
        brightness *= 1.0f - damping * 0.3f;
        const float qLoss = brightness * (2.0f - brightness) * 0.85f + 0.15f;

        for (size_t s = 0; s < size; ++s)
            out[s] = 0.0f;

        for (int i = 0; i < resolution_; ++i) {
            float modeFrequency = harmonic * stretchFactor;
            if (modeFrequency >= 0.499f)
                modeFrequency = 0.499f;
            const float modeAttenuation = 1.0f - modeFrequency * 2.0f;
            const float modeQ = 1.0f + modeFrequency * q;
            const float gain = modeAmplitude[static_cast<size_t>(i)] * modeAttenuation;

            modeFilters[static_cast<size_t>(i)].setFrequencyAndResonance(modeFrequency, modeQ);
            for (size_t s = 0; s < size; ++s)
                out[s] += gain * modeFilters[static_cast<size_t>(i)].process(in[s], Svf::Mode::BandPass);

            stretchFactor += stiffness;
            stiffness *= (stiffness < 0.0f) ? 0.93f : 0.98f;
            harmonic += f0;
            q *= qLoss;
        }
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;

    static float semitonesToRatio(float semitones) { return std::exp2(semitones / 12.0f); }
    static float interpolate(const float* table, float index, float size) {
        index *= size;
        const auto indexIntegral = static_cast<int>(index);
        const float indexFractional = index - static_cast<float>(indexIntegral);
        const float a = table[indexIntegral];
        const float b = table[indexIntegral + 1];
        return a + (b - a) * indexFractional;
    }

    // reference NthHarmonicCompensation(n, stiffness): runs the
    // SAME stretch_factor recurrence used in Process() for n-1
    // iterations, then returns 1/stretch_factor - used once, up front,
    // to pre-scale f0 so the 3rd harmonic (n=3, matching reference
    // fixed call `NthHarmonicCompensation(3, stiffness)`) lands
    // consistently in tune regardless of the stiffness setting.
    static float nthHarmonicCompensation(int n, float stiffness) {
        float stretchFactor = 1.0f;
        for (int i = 0; i < n - 1; ++i) {
            stretchFactor += stiffness;
            stiffness *= (stiffness < 0.0f) ? 0.93f : 0.98f;
        }
        return 1.0f / stretchFactor;
    }

    int resolution_ = kMaxNumModes;
    std::array<float, kMaxNumModes> modeAmplitude{};
    std::array<Svf, kMaxNumModes> modeFilters;
};

} //
