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

#include "Svf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ruidocore {

// AnalogHiHat DSP implementation.
// See THIRD_PARTY.md and the preserved source-license notice for attribution.
//
// Run the metallic cluster at twice the base frequency. Clocked-noise mixing
// and clock frequency both use squared noisiness; preserve the model clamps.
class AnalogHiHat {
public:
    void init(double sampleRate) {
        sampleRateHz = sampleRate;
        reset();
    }

    void reset() {
        for (auto& p : oscPhases)
            p = 0;
        toneFilter.reset();
        hpf.reset();
        noiseClockPhase = 0.0f;
        noiseSample = 0.0f;
        envelope = 0.0f;
        rngState = 0x9e3779b9u;
    }

    void trigger(float decay, float accent) {
        envelope = (1.5f + 0.5f * (1.0f - decay)) * (0.3f + 0.7f * std::clamp(accent, 0.0f, 1.0f));
    }

    // tone/noisiness/decay: 0..1 (see class comment for the knob
    // mapping this port assigns).
    float process(float f0, float tone, float noisiness, float decay) {
        // Six square oscillators at reference documented inharmonic
        // ratios (nominal reference 414Hz), summed to a -1..1 "metallic"
        // signal.
        static constexpr float kRatios[6] = {1.0f, 1.304f, 1.466f, 1.787f, 1.932f, 2.536f};
        const float metallicF0 = 2.0f * f0; // Run the metallic oscillator cluster at twice the base normalized frequency.
        int litCount = 0;
        for (int i = 0; i < 6; ++i) {
            const float oscFrequency = std::clamp(metallicF0 * kRatios[i], 0.0f, 0.499f);
            const auto increment = static_cast<std::uint32_t>(oscFrequency * 4294967296.0);
            oscPhases[static_cast<size_t>(i)] += increment;
            if (oscPhases[static_cast<size_t>(i)] & 0x80000000u)
                ++litCount;
        }
        const float metallic = 0.33f * static_cast<float>(litCount) - 1.0f;

        const float toneRatio = semitonesToRatio(tone * 72.0f); // 6-octave/72-semitone sweep, confirmed exact
        const float nyquistCap = 16000.0f / static_cast<float>(sampleRateHz); // reference upper clamp, not a generic 0.499 Nyquist cap
        const float bpfFrequency = std::clamp((150.0f * toneRatio) / static_cast<float>(sampleRateHz), 0.0f, nyquistCap);
        toneFilter.setFrequencyAndResonance(bpfFrequency, 3.0f + 3.0f * tone); // resonance=true config's real Q formula
        float out = toneFilter.process(metallic, Svf::Mode::BandPass);

        // Hold random values between clock ticks. f0 is already cycles/sample;
        // do not divide it by sampleRateHz again.
        const float noisinessSquared = noisiness * noisiness;
        const float noiseClockFrequency = std::clamp(f0 * (16.0f + 16.0f * (1.0f - noisinessSquared)), 0.0f, 0.5f);
        noiseClockPhase += noiseClockFrequency;
        if (noiseClockPhase >= 1.0f) {
            noiseClockPhase -= 1.0f;
            noiseSample = nextNoiseSample();
        }
        // Crossfade blend also uses the SQUARED noisiness (the same
        // reassigned value the clock-rate formula above uses), not the
        // raw knob value - reference reassigns its local `noisiness`
        // to its own square once, then reads it twice.
        out += noisinessSquared * (noiseSample - out);

        const float decayCoefficient = 1.0f - 0.003f * semitonesToRatio(-decay * 84.0f); // 1-0.003*2^(-decay*7), semitonesToRatio(-decay*84)=2^(-decay*7)
        envelope *= decayCoefficient;

        // SwingVCA, exact: asymmetric pre-scale on the RAW (pre-gain)
        // signal, soft-saturate, THEN add a +0.1 DC offset, THEN
        // multiply by the envelope gain - in that order.
        float swung = out * (out > 0.0f ? 4.0f : 0.1f);
        swung = swung / (1.0f + std::abs(swung));
        out = (swung + 0.1f) * envelope;

        // HPF: same tone-derived cutoff the BPF uses, but its OWN Svf
        // instance/state and a fixed Q=0.5 (per the researched formula -
        // not the tone-scaled resonance the BPF above uses).
        hpf.setFrequencyAndResonance(bpfFrequency, 0.5f);
        out = hpf.process(out, Svf::Mode::HighPass);

        return out;
    }

private:
    static float semitonesToRatio(float semitones) { return std::exp2(semitones / 12.0f); }

    float nextNoiseSample() {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return (static_cast<float>(rngState) / 4294967295.0f) * 2.0f - 1.0f;
    }

    double sampleRateHz = 48000.0;
    std::array<std::uint32_t, 6> oscPhases{};
    Svf toneFilter, hpf;
    float noiseClockPhase = 0.0f, noiseSample = 0.0f;
    float envelope = 0.0f;
    std::uint32_t rngState = 0x9e3779b9u;
};

} //
