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

#include "../Dsp/AnalogSnareDrum.h"
#include "../Dsp/DecayEnvelope.h"
#include "Engine.h"

#include <algorithm>

namespace ruidocore {

// Wraps Dsp/AnalogSnareDrum.h as a RuidoCore Engine - a faithful-effort
// port of Mutable Instruments' Plaits `SnareDrumEngine`
// (plaits/dsp/engine/snare_drum_engine.h/.cc, MIT licensed,
// github.com/pichenettes/eurorack), second of Percutere's first batch of
// dedicated percussion engines ("faça os percussivos primeiro" /
// "vamos lá", 2026-08-27).
//
// Knob mapping confirmed against the official Plaits manual (see
// AnalogSnareDrum.h's own header comment for the exact quotes and the
// harmonics/morph swap that check caught):
//   timbre    -> tone (mode-gain shaping)
//   harmonics -> snappy (shell vs noise balance)
//   morph     -> decay (mode Q/ring + noise envelope time)
//   note      -> f0
//   accent    -> exciter pulse height AND, via postProcessing.outGain,
//                overall level
//
// Real Plaits' own SnareDrumEngine renders TWO independent models in
// parallel (AnalogSnareDrum to `out`, a SyntheticSnareDrum to `aux`) -
// same scope-cut as BassDrumEngine.h: only the analog model is ported
// this pass; `aux` carries a simpler noise-only layer as a stand-in,
// porting the real SyntheticSnareDrum is a disclosed follow-up.
//
// Already self-enveloping (same reasoning as BassDrumEngine - the
// resonators' own Q-governed ring-out plus the noise envelope together
// ARE the amplitude envelope; the manual confirms the internal LPG is
// disabled for drum models).
class SnareDrumEngine : public Engine {
public:
    SnareDrumEngine() { postProcessing.alreadyEnveloped = true; }

    bool isActive() const override { return gateOpen; }
    float getEnvelopeValue01() const override { return decayEnvelope.value(); }

    void init(double sampleRate) override {
        sampleRateHz = sampleRate;
        drum.init(sampleRate);
        reset();
    }

    void reset() override {
        drum.reset();
        decayEnvelope.reset();
        activity = 0.0f;
        holdSamplesRemaining = 0;
        gateOpen = false;
    }

    void render(const EngineParameters& parameters, const float* /*in*/, float* out, float* aux,
                size_t numSamples) override {
        if (parameters.trigger == TriggerState::RisingEdge) {
            drum.trigger(std::clamp(parameters.accent, 0.0f, 1.0f));
            decayEnvelope.trigger();
            gateOpen = true;
            holdSamplesRemaining = static_cast<int>(2.0 * sampleRateHz); // same fixed-hold reasoning as BassDrumEngine
        }

        // Internal punch envelope, same block-rate approach and same
        // gap-closing reasoning as BassDrumEngine.h's own (see its
        // comment for why block-rate, not per-sample).
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

        const float f0 = noteToFrequencyHz(modulatedNote) / static_cast<float>(sampleRateHz);
        const float outputGain = std::clamp(parameters.accent, 0.0f, 1.0f) * postProcessing.outGain * levelEnvGain;

        for (size_t i = 0; i < numSamples; ++i) {
            if (!gateOpen) {
                out[i] = 0.0f;
                if (aux)
                    aux[i] = 0.0f;
                continue;
            }

            const float raw = drum.process(f0, modulatedTimbre, modulatedHarmonics, modulatedMorph);

            out[i] = raw * outputGain;
            if (aux)
                aux[i] = raw * outputGain; // stand-in for the not-yet-ported SyntheticSnareDrum, see class comment

            activity = std::max(activity * 0.9995f, std::abs(raw));
            if (holdSamplesRemaining > 0)
                --holdSamplesRemaining;
            else if (activity < 0.0008f)
                gateOpen = false;
        }
    }

private:
    double sampleRateHz = 48000.0;
    AnalogSnareDrum drum;
    DecayEnvelope decayEnvelope;
    float activity = 0.0f;
    int holdSamplesRemaining = 0;
    bool gateOpen = false;
};

} // namespace ruidocore
