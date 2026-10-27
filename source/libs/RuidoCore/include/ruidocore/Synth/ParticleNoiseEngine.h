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

#include "../Dsp/DecayEnvelope.h"
#include "../Dsp/LowPassGate.h"
#include "../Dsp/Particle.h"
#include "../Dsp/SimpleDiffuser.h"
#include "../Dsp/Svf.h"
#include "Engine.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ruidocore {

// A faithful port of Mutable Instruments' Plaits `ParticleEngine`
// (plaits/dsp/engine/particle_engine.h/.cc, "Filtered random pulses",
// MIT licensed, github.com/pichenettes/eurorack, downloaded and read
// directly) - `kNumParticles` (6) `Particle` instances (see
// Dsp/Particle.h) summed together, each independently re-randomizing
// its own resonant filter frequency on every impulse it fires. The one
// deliberate deviation is the post-mix diffuser: real Plaits' own
// `Diffuser` runs a custom delay-line DSL disproportionate to port for
// this one knob - see Dsp/SimpleDiffuser.h's own header for exactly
// what's kept vs simplified there.
//
// Parameter mapping - matches the official Plaits manual's own
// description for this model ("Harmonics: spread of the resonant
// frequencies; Timbre: density of the pulses; Morph: below noon,
// filter Q with diffusion added; above noon, resonance only"):
//   harmonics -> spread (how far each impulse's own re-randomized
//                filter frequency can wander from the base pitch)
//   timbre    -> density (how often impulses fire)
//   morph     -> resonance (upper half) / diffusion amount (lower half)
//   decay     -> LPG gain/brightness decay time
//   accent    -> output level only
class ParticleNoiseEngine : public Engine {
public:
    ParticleNoiseEngine() { postProcessing.alreadyEnveloped = true; }

    bool isActive() const override { return gateOpen; }
    float getEnvelopeValue01() const override { return decayEnvelope.value(); }

    void init(double sampleRate) override {
        sampleRateHz = sampleRate;
        lpg.reset();
        reset();
    }

    void reset() override {
        for (auto& p : particles)
            p = Particle();
        postFilter.reset();
        diffuser.reset();
        lpgEnvelope.reset();
        decayEnvelope.reset();
        gateOpen = false;
    }

    void render(const EngineParameters& parameters, const float* /*in*/, float* out, float* aux,
                size_t numSamples) override {
        bool sync = false;
        if (parameters.trigger == TriggerState::RisingEdge) {
            lpgEnvelope.trigger();
            decayEnvelope.trigger();
            gateOpen = true;
            sync = true;
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
            // Widened on the POSITIVE side (2026-08-28, "o de level ainda parece
            // inefetivo" - the old +-1x symmetric range only reached +6dB at max
            // positive amount, easily masked by this engine's own natural decay;
            // negative already reaches full silence at max amount so stays as-is).
            const float levelEnvGain = parameters.levelEnvAmount >= 0.0f
                                            ? std::clamp(1.0f + parameters.levelEnvAmount * envValue * 3.0f, 0.0f, 4.0f)
                                            : std::clamp(1.0f + parameters.levelEnvAmount * envValue, 0.0f, 2.0f);

            const float f0 = noteToFrequencyHz(parameters.note + pitchEnvSemitones) / static_cast<float>(sampleRateHz);
            const float densitySqrt = noteToFrequencyHz(60.0f + sampleTimbre * sampleTimbre * 72.0f) / static_cast<float>(sampleRateHz);
            const float density = densitySqrt * densitySqrt * (1.0f / kNumParticles);
            const float gain = 1.0f / std::max(density, 1e-9f);
            const float qSqrt = semitonesToRatio(sampleMorph >= 0.5f ? (sampleMorph - 0.5f) * 120.0f : 0.0f);
            const float q = 0.5f + qSqrt * qSqrt;
            const float spread = 48.0f * sampleHarmonics * sampleHarmonics;
            const float rawDiffusionSqrt = 2.0f * std::abs(sampleMorph - 0.5f);
            const float rawDiffusion = rawDiffusionSqrt * rawDiffusionSqrt;
            const float diffusion = sampleMorph < 0.5f ? rawDiffusion : 0.0f;

            std::fill(scratchOut.begin(), scratchOut.begin() + static_cast<long>(blockLength), 0.0f);
            std::fill(scratchAux.begin(), scratchAux.begin() + static_cast<long>(blockLength), 0.0f);
            for (auto& p : particles)
                p.render(sync, density, gain, f0, spread, q, scratchOut.data(), scratchAux.data(), blockLength);
            sync = false; // only the block a trigger lands in re-syncs each particle's own phase

            postFilter.setFrequencyAndResonance(std::min(f0, 0.49f), 0.5f);
            for (size_t j = 0; j < blockLength; ++j)
                scratchOut[j] = postFilter.process(scratchOut[j], Svf::Mode::LowPass);
            diffuser.process(0.8f * diffusion * diffusion, 0.5f * diffusion + 0.25f, scratchOut.data(), blockLength);

            for (size_t j = 0; j < blockLength; ++j, ++i) {
                const float gated = lpg.process(scratchOut[j], lpgEnvelope.gain(), lpgEnvelope.frequency(), lpgEnvelope.hfBleed());
                out[i] = gated * outputGain * levelEnvGain;
                if (aux)
                    aux[i] = scratchAux[j] * lpgEnvelope.gain() * outputGain * levelEnvGain;

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
    static constexpr int kNumParticles = 6;

    double sampleRateHz = 48000.0;
    std::array<Particle, kNumParticles> particles;
    std::array<float, kControlBlockSize> scratchOut{}, scratchAux{};
    Svf postFilter;
    SimpleDiffuser diffuser;
    LpgEnvelope lpgEnvelope;
    LowPassGate lpg;
    DecayEnvelope decayEnvelope;
    bool gateOpen = false;
};

} // namespace ruidocore
