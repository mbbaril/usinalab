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

// AnalogSnareDrum DSP implementation.
// See THIRD_PARTY.md and the preserved source-license notice for attribution.
//
// Timbre controls modal gain balance, harmonics controls shell/noise mix,
// and morph controls modal decay and noise-envelope time.
//
// Preserve the curved decay-to-Q mapping, repeated-squaring modal gains
// and separate transient paths for the fundamental and higher modes.
class AnalogSnareDrum {
public:
    void init(double sampleRate) {
        sampleRateHz = sampleRate;
        reset();
    }

    void reset() {
        for (auto& r : resonators)
            r.reset();
        noiseFilter.reset();
        pulseRemaining = 0;
        pulse = 0.0f;
        pulseHeight = 0.0f;
        pulseLp = 0.0f;
        noiseEnvelope = 0.0f;
        rngState = 0x1234567u;
    }

    void trigger(float accent) {
        pulseRemaining = static_cast<int>(1.0e-3 * sampleRateHz); // ~1ms, same convention as AnalogBassDrum
        pulseHeight = 3.0f + 7.0f * std::clamp(accent, 0.0f, 1.0f);
        noiseEnvelope = 2.0f; // confirmed via research, not 1.0 - the *2.0f already in the noise-scaling formula compounds with this
    }

    // tone/snappy/decay: 0..1 - see class comment for the knob mapping
    // this port assigns.
    float process(float f0, float tone, float snappy, float decay) {
        // Curved remap of `decay` that feeds `q` - matches reference
        // own `decay_xt` exactly (a mild S-curve, not just raw decay).
        const float decayXt = decay * (1.0f + decay * (decay - 1.0f));
        // Un-filtered leak term mixed directly into each mode's own
        // contribution below - computed from RAW (pre-adjustment)
        // snappy, same timing as noiseEnvelopeDecay's own raw-snappy use.
        const float exciterLeak = snappy * (2.0f - snappy) * 0.1f;
        const float snappyAdjusted = std::clamp(1.1f * snappy - 0.05f, 0.0f, 1.0f);

        const float pulseDecayTime = static_cast<float>(0.1e-3 * sampleRateHz);
        if (pulseRemaining > 0) {
            --pulseRemaining;
            pulse = pulseRemaining > 0 ? pulseHeight : (pulseHeight - 1.0f);
        } else {
            pulse *= (1.0f - 1.0f / pulseDecayTime);
        }
        pulseLp += (pulse - pulseLp) * 0.75f; // fixed one-pole pole (0.75), per the researched constant
        const float pulseHp = pulse - pulseLp;

        // Mode gains - piecewise in `tone`, ported from the researched
        // formula exactly.
        std::array<float, 5> gain{};
        if (tone < (2.0f / 3.0f)) {
            gain[0] = 1.5f + (1.0f - 1.5f * tone) * (1.0f - 1.5f * tone) * 4.5f;
            gain[1] = 3.0f * tone + 0.15f;
            gain[2] = gain[3] = gain[4] = 0.0f;
        } else {
            float t = 3.0f * (tone - 2.0f / 3.0f);
            gain[0] = 1.5f - 0.5f * t;
            gain[1] = 2.15f - 0.7f * t;
            // Repeated squaring (t, t^2, t^4), matching reference
            // `for (i=2..): gain[i]=tone; tone*=tone;` exactly - NOT
            // t^(m-1) (that would make gain[4] = t^3, which is wrong).
            for (int m = 2; m < 5; ++m) {
                gain[static_cast<size_t>(m)] = t;
                t *= t;
            }
        }

        static constexpr float kModeRatios[5] = {1.00f, 2.00f, 3.18f, 4.16f, 5.62f};
        const float q = 2000.0f * semitonesToRatio(decayXt * 84.0f);

        float shell = 0.0f;
        for (int m = 0; m < 5; ++m) {
            const float modeFrequency = std::clamp(f0 * kModeRatios[m], 0.0f, 0.499f);
            // `q` alone (up to ~2000*2^7 = 256000) is nowhere near a
            // usable resonance value for this library's own Svf
            // convention (r = 1/resonance - a q that large is
            // effectively undamped/self-oscillating, ringing forever
            // instead of decaying - caught from a real by-ear test,
            // "parece ter um oscilador em sustain sempre vazando").
            // AnalogBassDrum's own (already-verified) sibling formula
            // tempers its own analogous `q` the same way, multiplying by
            // f0 before adding 1 - applied here too, for the same
            // physical reason (a resonator's usable Q scales with how
            // many cycles-per-sample its own frequency spans, not as a
            // bare knob-derived number).
            const float modeQ = 1.0f + (m == 0 ? q : q * 0.25f) * modeFrequency;
            resonators[static_cast<size_t>(m)].setFrequencyAndResonance(modeFrequency, modeQ);
            // Mode 0 (fundamental) gets the sharp high-passed transient
            // plus a small copy of the raw pulse; modes 1-4 get an even
            // smaller fixed fraction of the raw pulse - reference
            // exact constants (0.006f/0.026f), not guessed.
            const float excitation = m == 0 ? (pulseHp + pulse * 0.006f) : (pulse * 0.026f);
            const float resonated = resonators[static_cast<size_t>(m)].process(excitation, Svf::Mode::BandPass);
            shell += gain[static_cast<size_t>(m)] * (resonated + excitation * exciterLeak);
        }
        shell = softClip(shell);

        // Noise: half-wave-rectified white noise, scaled by its own
        // envelope AND snappy BEFORE filtering (not after - the exact
        // order matters here since the envelope is time-varying, caught
        // via a second, more targeted research pass after the first
        // pass's summary got the order wrong and invented an
        // "exciter_leak" term that isn't actually part of this specific
        // mix formula), then through a resonant band-pass tuned well
        // above the fundamental ("fizzle").
        float noiseSample = std::max(nextNoiseSample(), 0.0f);
        // Decay coefficient uses the RAW (pre-adjustment) snappy value -
        // confirmed via research: the `snappy = snappy*1.1-0.05`
        // adjustment happens AFTER this coefficient is computed.
        const float noiseEnvelopeDecay = 1.0f - 0.0017f * semitonesToRatio(-decay * (50.0f + snappy * 10.0f));
        noiseEnvelope *= noiseEnvelopeDecay;
        noiseSample *= noiseEnvelope * snappyAdjusted * 2.0f;

        const float noiseFrequency = std::clamp(16.0f * f0, 0.0f, 0.499f);
        noiseFilter.setFrequencyAndResonance(noiseFrequency, 1.0f + 1.5f * noiseFrequency);
        const float filteredNoise = noiseFilter.process(noiseSample, Svf::Mode::BandPass);

        return filteredNoise + shell * (1.0f - snappyAdjusted);
    }

private:
    static float semitonesToRatio(float semitones) { return std::exp2(semitones / 12.0f); }
    // AnalogSnareDrum DSP implementation.
    // See THIRD_PARTY.md and the preserved source-license notice for attribution.
    static float softClip(float x) {
        if (x < -3.0f)
            return -1.0f;
        if (x > 3.0f)
            return 1.0f;
        return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
    }

    // Simple xorshift PRNG - white noise source, no need to match
    // reference generator bit-for-bit (only the resulting spectral/
    // statistical character matters for this use).
    float nextNoiseSample() {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return (static_cast<float>(rngState) / 4294967295.0f) * 2.0f - 1.0f;
    }

    double sampleRateHz = 48000.0;
    std::array<Svf, 5> resonators;
    Svf noiseFilter;
    int pulseRemaining = 0;
    float pulse = 0.0f, pulseHeight = 0.0f, pulseLp = 0.0f;
    float noiseEnvelope = 0.0f;
    std::uint32_t rngState = 0x1234567u;
};

} //
