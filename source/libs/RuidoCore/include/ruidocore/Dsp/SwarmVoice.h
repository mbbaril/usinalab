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

#include "ClockedNoise.h" // for thisBlepSample/nextBlepSample

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ruidocore {

// SwarmVoice DSP implementation.
// See THIRD_PARTY.md and the preserved source-license notice for attribution.
class SwarmRandom {
public:
    static float next01() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state) / 4294967295.0f;
    }

private:
    static inline std::uint32_t state = 0x6d2b79f5u;
};

// One grain's own timing/amplitude state machine - free-runs between
// grains ("cloud" mode when `sizeRatio < 1`, a continuous glissando
// tone ramping between two randomly-chosen frequencies) or fires bursts
// in lockstep with the host trigger (`burstMode`).
class GrainEnvelope {
public:
    void init() {
        from = 0.0f;
        interval = 1.0f;
        phase = 1.0f;
        fm = 0.0f;
        amplitude = 0.5f;
        previousSizeRatio = 0.0f;
        filterCoefficient = 0.0f;
    }

    void step(float rate, bool burstMode, bool startBurst) {
        bool randomize = false;
        if (startBurst) {
            phase = 0.5f;
            fm = 16.0f;
            randomize = true;
        } else {
            phase += rate * fm;
            if (phase >= 1.0f) {
                phase -= std::floor(phase);
                randomize = true;
            }
        }

        if (randomize) {
            from += interval;
            interval = SwarmRandom::next01() - from;
            if (burstMode)
                fm *= 0.8f + 0.2f * SwarmRandom::next01();
            else
                fm = 0.5f + 1.5f * SwarmRandom::next01();
        }
    }

    float frequency(float sizeRatio) const {
        if (sizeRatio < 1.0f)
            return 2.0f * (from + interval * phase) - 1.0f;
        return from;
    }

    float amplitudeValue(float sizeRatio) {
        float targetAmplitude = 1.0f;
        if (sizeRatio >= 1.0f) {
            float p = (phase - 0.5f) * sizeRatio;
            p = std::clamp(p, -1.0f, 1.0f);
            // reference `Sine(phase)` (a 512-entry LUT lookup) -
            // std::sin is this port's established direct substitute
            // (see FmEngine.h's own precedent), same convention.
            const float e = std::sin(2.0f * kPi * (0.5f * p + 1.25f));
            targetAmplitude = 0.5f * (e + 1.0f);
        }

        if ((sizeRatio >= 1.0f) != (previousSizeRatio >= 1.0f))
            filterCoefficient = 0.5f;
        filterCoefficient *= 0.95f;

        previousSizeRatio = sizeRatio;
        amplitude += (targetAmplitude - amplitude) * (0.5f - filterCoefficient);
        return amplitude;
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;
    float from = 0.0f, interval = 1.0f, phase = 1.0f, fm = 0.0f;
    float amplitude = 0.5f, previousSizeRatio = 0.0f, filterCoefficient = 0.0f;
};

// PolyBLEP-corrected sawtooth, additively accumulated into the output
// buffer (matches reference `+=` accumulation, since several
// SwarmVoice instances share one output buffer).
class AdditiveSawOscillator {
public:
    void init() {
        phase = 0.0f;
        nextSample = 0.0f;
        frequency = 0.01f;
    }

    void render(float targetFrequency, float level, float* out, size_t size) {
        frequency = std::min(targetFrequency, 0.25f); // kMaxFrequency
        while (size--) {
            float thisSample = nextSample;
            nextSample = 0.0f;

            phase += frequency;
            if (phase >= 1.0f) {
                phase -= 1.0f;
                const float t = phase / frequency;
                thisSample -= thisBlepSample(t);
                nextSample -= nextBlepSample(t);
            }
            nextSample += phase;
            *out++ += (2.0f * thisSample - 1.0f) * level;
        }
    }

private:
    float phase = 0.0f, nextSample = 0.0f, frequency = 0.01f;
};

// "Magic circle" coupled-form sine oscillator - a self-correcting
// rotation-matrix recurrence, exact port of reference
// `FastSineOscillator` (ADDITIVE mode only - the mode SwarmVoice uses).
class MagicCircleSineOscillator {
public:
    void init() {
        x = 1.0f;
        y = 0.0f;
    }

    void render(float frequency, float amplitude, float* out, size_t size) {
        if (frequency >= 0.25f) {
            frequency = 0.25f;
            amplitude = 0.0f;
        } else {
            amplitude *= 1.0f - frequency * 4.0f;
        }

        const float fPi = frequency * kPi;
        const float epsilon = fPi * (2.0f - (2.0f * 0.96f / 6.0f) * fPi * fPi); // Fast2Sin, reference 3rd-order approximation

        const float norm = x * x + y * y;
        if (norm <= 0.5f || norm >= 2.0f) {
            const float scale = 1.0f / std::sqrt(norm);
            x *= scale;
            y *= scale;
        }

        while (size--) {
            x += epsilon * y;
            y -= epsilon * x;
            *out++ += amplitude * x;
        }
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;
    float x = 1.0f, y = 0.0f;
};

// One of `kNumSwarmVoices` (8) ranked voices - `rank` runs from -1..1
// across the ensemble, spreading each voice's own pitch away from f0 by
// an amount that grows with both `rank` and the `spread` knob.
class SwarmVoice {
public:
    void init(float voiceRank) {
        rank = voiceRank;
        envelope.init();
        saw.init();
        sine.init();
    }

    void render(float f0, float density, bool burstMode, bool startBurst, float spread, float sizeRatio, float* sawOut,
                float* sineOut, size_t size) {
        envelope.step(density, burstMode, startBurst);

        constexpr float kScale = 1.0f / 8.0f; // 1/kNumSwarmVoices
        const float amplitude = envelope.amplitudeValue(sizeRatio) * kScale;

        const float expoAmount = envelope.frequency(sizeRatio);
        float voiceF0 = f0 * std::exp2(48.0f * expoAmount * spread * rank / 12.0f);

        const float linearAmount = rank * (rank + 0.01f) * spread * 0.25f;
        voiceF0 *= 1.0f + linearAmount;

        saw.render(voiceF0, amplitude, sawOut, size);
        sine.render(voiceF0, amplitude, sineOut, size);
    }

private:
    float rank = 0.0f;
    GrainEnvelope envelope;
    AdditiveSawOscillator saw;
    MagicCircleSineOscillator sine;
};

} //
