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

#include "../Dsp/AnalogHiHat.h"
#include "../Dsp/DecayEnvelope.h"
#include "Engine.h"

#include <algorithm>

namespace ruidocore {

// Wraps Dsp/AnalogHiHat.h as a RuidoCore Engine - a faithful-effort port
// of Mutable Instruments' Plaits `HiHatEngine`
// (plaits/dsp/engine/hi_hat_engine.h/.cc, MIT licensed,
// github.com/pichenettes/eurorack), third of Percutere's first batch of
// dedicated percussion engines.
//
// Knob mapping confirmed against the official Plaits manual: harmonics
// -> noisiness ("balance of the metallic and filtered noise"), timbre
// -> tone ("high-pass filter cutoff"), morph -> decay ("decay time").
//
// Real Plaits' own HiHatEngine renders TWO differently-configured HiHat
// instances in parallel (`hi_hat_1_`: SquareNoise/SwingVCA/resonant, to
// `out`; `hi_hat_2_`: RingModNoise/LinearVCA/two-stage-envelope, to
// `aux`) - this pass only ports the first (`out`) configuration;
// `aux` carries a simpler pre-envelope copy of the metallic+noise mix
// as a stand-in, porting the real second HiHat variant is a disclosed
// follow-up (same scope-cut pattern as BassDrumEngine/SnareDrumEngine).
//
// Already self-enveloping (own decay envelope + VCA, no external LPG -
// matches the rest of this batch).
class HiHatEngine : public Engine {
public:
    HiHatEngine() { postProcessing.alreadyEnveloped = true; }

    bool isActive() const override { return gateOpen; }
    float getEnvelopeValue01() const override { return decayEnvelope.value(); }

    void init(double sampleRate) override {
        sampleRateHz = sampleRate;
        hat.init(sampleRate);
        reset();
    }

    void reset() override {
        hat.reset();
        decayEnvelope.reset();
        activity = 0.0f;
        holdSamplesRemaining = 0;
        gateOpen = false;
    }

    void render(const EngineParameters& parameters, const float* /*in*/, float* out, float* aux,
                size_t numSamples) override {
        if (parameters.trigger == TriggerState::RisingEdge)
            decayEnvelope.trigger(); // before reading envValue below, so trigger() and process() both see the fresh hit

        // Internal punch envelope, same block-rate approach and same
        // gap-closing reasoning as BassDrumEngine.h's own.
        const float shortDecay = (200.0f * 24.0f / static_cast<float>(sampleRateHz)) *
                                  semitonesToRatio(-96.0f * parameters.decay);
        decayEnvelope.process(shortDecay * 2.0f);
        const float envValue = decayEnvelope.value();

        const float modulatedNote =
            std::clamp(parameters.note + parameters.pitchEnvAmount * envValue * envValue * 48.0f, 0.0f, 127.0f);
        const float modulatedHarmonics = std::clamp(parameters.harmonics + parameters.harmonicsEnvAmount * envValue, 0.0f, 1.0f);
        const float modulatedTimbre = std::clamp(parameters.timbre + parameters.timbreEnvAmount * envValue, 0.0f, 1.0f);
        const float modulatedMorph = std::clamp(parameters.morph + parameters.morphEnvAmount * envValue, 0.0f, 1.0f);
        // Widened on the POSITIVE side (2026-08-28, "o de level ainda parece
        // inefetivo" - the old +-1x symmetric range only reached +6dB at max
        // positive amount, easily masked by this engine's own natural decay;
        // negative already reaches full silence at max amount so stays as-is).
        const float levelEnvGain = parameters.levelEnvAmount >= 0.0f
                                        ? std::clamp(1.0f + parameters.levelEnvAmount * envValue * 3.0f, 0.0f, 4.0f)
                                        : std::clamp(1.0f + parameters.levelEnvAmount * envValue, 0.0f, 2.0f);

        if (parameters.trigger == TriggerState::RisingEdge) {
            hat.trigger(modulatedMorph, std::clamp(parameters.accent, 0.0f, 1.0f));
            gateOpen = true;
            holdSamplesRemaining = static_cast<int>(1.0 * sampleRateHz); // hi-hats are inherently shorter than the kick/snare's own hold window
        }

        const float f0 = noteToFrequencyHz(modulatedNote) / static_cast<float>(sampleRateHz);
        const float outputGain = std::clamp(parameters.accent, 0.0f, 1.0f) * postProcessing.outGain * levelEnvGain;

        for (size_t i = 0; i < numSamples; ++i) {
            if (!gateOpen) {
                out[i] = 0.0f;
                if (aux)
                    aux[i] = 0.0f;
                continue;
            }

            const float raw = hat.process(f0, modulatedTimbre, modulatedHarmonics, modulatedMorph);

            out[i] = raw * outputGain;
            if (aux)
                aux[i] = raw * outputGain; // stand-in for the not-yet-ported second HiHat variant, see class comment

            activity = std::max(activity * 0.9995f, std::abs(raw));
            if (holdSamplesRemaining > 0)
                --holdSamplesRemaining;
            else if (activity < 0.0008f)
                gateOpen = false;
        }
    }

private:
    double sampleRateHz = 48000.0;
    AnalogHiHat hat;
    DecayEnvelope decayEnvelope;
    float activity = 0.0f;
    int holdSamplesRemaining = 0;
    bool gateOpen = false;
};

} // namespace ruidocore
