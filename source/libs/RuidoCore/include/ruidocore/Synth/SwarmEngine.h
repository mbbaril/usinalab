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

#include "../Dsp/DecayEnvelope.h"
#include "../Dsp/LowPassGate.h"
#include "../Dsp/SwarmVoice.h"
#include "Engine.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ruidocore {

// SwarmEngine DSP implementation.
// See THIRD_PARTY.md and the preserved source-license notice for attribution.
//
// Trigger-vs-CV semantics: reference `burst_mode = !(trigger &
// TRIGGER_UNPATCHED)` is true only when a cable IS physically patched
// into the trigger input - this library has no unpatched/CV-free-
// running concept at all (every voice is always internally triggered
// by the sequencer, the software equivalent of "trigger always
// patched"), so this port always takes `burstMode = true` (matching
// AnalogHiHat/FilteredNoiseEngine's own identical reasoning for their
// own patched-vs-unpatched branches).
//
// Grain-rate/block-size coupling: reference `density` formula
// scales directly with `control_rate` (the render() call's OWN block
// size, in samples) - GrainEnvelope::Step() is called once per
// render() call, not once per sample, so how large a block this port
// processes at a time genuinely changes the grain-triggering rate, not
// just an implementation detail. Uses the same kControlBlockSize=24
// this whole family's other block-rate engines already standardize on
// (WavetableEngine/FilteredNoiseEngine/ParticleNoiseEngine), matching
// reference control-block cadence.
//
// Parameter mapping - matches the official reference manual's own
// description ("Harmonics: spread; Timbre: grain density; Morph:
// grain/glissando size"):
// harmonics -> spread (how far each voice's own pitch wanders from f0)
// timbre -> grain density (how often new grains/glissandi start)
// morph -> grain size (below noon: short grain "cloud"; above
// noon: continuous glissando between random pitches)
// decay -> LPG gain/brightness decay time
// accent -> output level only
class SwarmEngine : public Engine {
public:
    SwarmEngine() {
        postProcessing.alreadyEnveloped = true;
        const float n = (kNumSwarmVoices - 1) / 2.0f;
        for (int i = 0; i < kNumSwarmVoices; ++i)
            voices[static_cast<size_t>(i)].init((static_cast<float>(i) - n) / n);
    }

    bool isActive() const override { return gateOpen; }
    float getEnvelopeValue01() const override { return decayEnvelope.value(); }

    void init(double sampleRate) override {
        sampleRateHz = sampleRate;
        lpg.reset();
        reset();
    }

    void reset() override {
        lpgEnvelope.reset();
        decayEnvelope.reset();
        gateOpen = false;
    }

    void render(const EngineParameters& parameters, const float* /*in*/, float* out, float* aux,
                size_t numSamples) override {
        bool startBurst = false;
        if (parameters.trigger == TriggerState::RisingEdge) {
            lpgEnvelope.trigger();
            decayEnvelope.trigger();
            gateOpen = true;
            startBurst = true;
        }

        const float outputGain = std::clamp(parameters.accent, 0.0f, 1.0f) * postProcessing.outGain;
        const float hf = 0.3f;

        size_t i = 0;
        while (i < numSamples) {
            if (!gateOpen) {
                for (; i < numSamples; ++i) {
                    out[i] = 0.0f;
                    if (aux)
                        aux[i] = 0.0f;
                }
                break;
            }

            const size_t blockLength = std::min(static_cast<size_t>(kControlBlockSize), numSamples - i);

            const float shortDecay =
                (200.0f * kControlBlockSize / static_cast<float>(sampleRateHz)) * semitonesToRatio(-96.0f * parameters.decay);
            const float decayTail = (20.0f * kControlBlockSize / static_cast<float>(sampleRateHz)) *
                                         semitonesToRatio(-72.0f * parameters.decay + 12.0f * hf) -
                                     shortDecay;
            const float attack = (noteToFrequencyHz(parameters.note) / static_cast<float>(sampleRateHz)) * kControlBlockSize * 2.0f;
            lpgEnvelope.processPing(attack, shortDecay, decayTail, hf);

            decayEnvelope.process(shortDecay * 2.0f);
            const float envValue = decayEnvelope.value();
            const float pitchEnvSemitones = parameters.pitchEnvAmount * envValue * envValue * 48.0f;
            const float sampleTimbre = std::clamp(parameters.timbre + parameters.timbreEnvAmount * envValue, 0.0f, 1.0f);
            const float sampleMorph = std::clamp(parameters.morph + parameters.morphEnvAmount * envValue, 0.0f, 1.0f);
            const float sampleHarmonics = std::clamp(parameters.harmonics + parameters.harmonicsEnvAmount * envValue, 0.0f, 1.0f);
            // Envelope depth is bipolar; positive depth uses the extended gain range.
            const float levelEnvGain = parameters.levelEnvAmount >= 0.0f
                                            ? std::clamp(1.0f + parameters.levelEnvAmount * envValue * 3.0f, 0.0f, 4.0f)
                                            : std::clamp(1.0f + parameters.levelEnvAmount * envValue, 0.0f, 2.0f);

            const float f0 = noteToFrequencyHz(parameters.note + pitchEnvSemitones) / static_cast<float>(sampleRateHz);
            const float controlRate = static_cast<float>(blockLength);
            const float density =
                (noteToFrequencyHz(60.0f + sampleTimbre * 120.0f) / static_cast<float>(sampleRateHz)) * 0.025f * controlRate;
            const float spread = sampleHarmonics * sampleHarmonics * sampleHarmonics;
            float sizeRatio = 0.25f * semitonesToRatio((1.0f - sampleMorph) * 84.0f);

            std::fill(scratchSaw.begin(), scratchSaw.begin() + static_cast<long>(blockLength), 0.0f);
            std::fill(scratchSine.begin(), scratchSine.begin() + static_cast<long>(blockLength), 0.0f);
            for (auto& v : voices) {
                v.render(f0, density, true, startBurst, spread, sizeRatio, scratchSaw.data(), scratchSine.data(), blockLength);
                sizeRatio *= 0.97f;
            }
            startBurst = false; // only the block the trigger lands in starts a new burst

            for (size_t j = 0; j < blockLength; ++j, ++i) {
                const float gated = lpg.process(scratchSaw[j], lpgEnvelope.gain(), lpgEnvelope.frequency(), lpgEnvelope.hfBleed());
                out[i] = gated * outputGain * levelEnvGain;
                if (aux)
                    aux[i] = scratchSine[j] * lpgEnvelope.gain() * outputGain * levelEnvGain;

                if (lpgEnvelope.gain() < 0.0005f && !lpgEnvelope.isRampingUp()) {
                    gateOpen = false;
                    ++i;
                    for (; i < numSamples; ++i) {
                        out[i] = 0.0f;
                        if (aux)
                            aux[i] = 0.0f;
                    }
                    return;
                }
            }
        }
    }

private:
    static constexpr int kControlBlockSize = 24;
    static constexpr int kNumSwarmVoices = 8;

    double sampleRateHz = 48000.0;
    std::array<SwarmVoice, kNumSwarmVoices> voices;
    std::array<float, kControlBlockSize> scratchSaw{}, scratchSine{};
    LpgEnvelope lpgEnvelope;
    LowPassGate lpg;
    DecayEnvelope decayEnvelope;
    bool gateOpen = false;
};

} //
