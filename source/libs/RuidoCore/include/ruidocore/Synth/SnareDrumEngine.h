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

#include "../Dsp/AnalogSnareDrum.h"
#include "../Dsp/DecayEnvelope.h"
#include "Engine.h"

#include <algorithm>

namespace ruidocore {

// See THIRD_PARTY.md and the preserved license notice for source provenance.
//
// Knob mapping confirmed against the official reference manual (see
// AnalogSnareDrum.h's own header comment for the exact quotes and the
// harmonics/morph swap that check caught):
// timbre -> tone (mode-gain shaping)
// harmonics -> snappy (shell vs noise balance)
// morph -> decay (mode Q/ring + noise envelope time)
// note -> f0
// accent -> exciter pulse height AND, via postProcessing.outGain,
// overall level
//
// reference SnareDrumEngine renders TWO independent models in
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

        // Update the shared decay envelope at control-block cadence and apply enabled
        // parameter depths to the engine controls.
        const float shortDecay = (200.0f * 24.0f / static_cast<float>(sampleRateHz)) *
                                  semitonesToRatio(-96.0f * parameters.decay);
        decayEnvelope.process(shortDecay * 2.0f);
        const float envValue = decayEnvelope.value();

        const float modulatedNote =
            std::clamp(parameters.note + parameters.pitchEnvAmount * envValue * envValue * 48.0f, 0.0f, 127.0f);
        const float modulatedHarmonics = std::clamp(parameters.harmonics + parameters.harmonicsEnvAmount * envValue, 0.0f, 1.0f);
        const float modulatedTimbre = std::clamp(parameters.timbre + parameters.timbreEnvAmount * envValue, 0.0f, 1.0f);
        const float modulatedMorph = std::clamp(parameters.morph + parameters.morphEnvAmount * envValue, 0.0f, 1.0f);
        // Envelope depth is bipolar; positive depth uses the extended gain range.
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

} //
