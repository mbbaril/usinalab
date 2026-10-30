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
#include "../Dsp/Resonator.h"
#include "../Dsp/Svf.h"
#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ruidocore {

// Struck modal synthesis using a resonator bank and exciter.
// Source attribution is preserved in the license notice and THIRD_PARTY.md.
//
// Harmonics selects structure, timbre controls brightness and morph controls damping.
// Smooth structure with one-pole coefficient 0.01 to limit abrupt modal changes.
//
// NOT ported: reference "sustain mode" (continuous dust-
// noise excitation instead of a single click) - same scope-cut as
// every other engine in this library (always percussive/triggered).
// Already self-enveloping (the resonator bank's own per-mode Q-governed
// ring-out IS the amplitude envelope - matches the manual's "internal
// LPG disabled for physical...models" note, same as the drum engines).
class ModalEngine : public Engine {
public:
    ModalEngine() { postProcessing.alreadyEnveloped = true; }

    bool isActive() const override { return gateOpen; }
    float getEnvelopeValue01() const override { return decayEnvelope.value(); }

    void init(double sampleRate) override {
        sampleRateHz = sampleRate;
        resonator.init(0.015f, Resonator::kMaxNumModes); // reference fixed excitation position (0.015) - see class comment
        reset();
    }

    void reset() override {
        excitationFilter.reset();
        decayEnvelope.reset();
        harmonicsLp = 0.0f;
        activity = 0.0f;
        holdSamplesRemaining = 0;
        gateOpen = false;
    }

    void render(const EngineParameters& parameters, const float* /*in*/, float* out, float* aux,
                size_t numSamples) override {
        const bool triggerNow = parameters.trigger == TriggerState::RisingEdge;
        if (triggerNow) {
            decayEnvelope.trigger();
            gateOpen = true;
            holdSamplesRemaining = static_cast<int>(2.0 * sampleRateHz);
        }

        // Update the shared decay envelope at control-block cadence and apply enabled
        // parameter depths to the engine controls.
        const float shortDecay = (200.0f * 24.0f / static_cast<float>(sampleRateHz)) *
                                  semitonesToRatio(-96.0f * parameters.decay);
        decayEnvelope.process(shortDecay * 2.0f);
        const float envValue = decayEnvelope.value();
        const float modulatedNote =
            std::clamp(parameters.note + parameters.pitchEnvAmount * envValue * envValue * 48.0f, 0.0f, 127.0f);
        // Envelope depth is bipolar; positive depth uses the extended gain range.
        const float levelEnvGain = parameters.levelEnvAmount >= 0.0f
                                        ? std::clamp(1.0f + parameters.levelEnvAmount * envValue * 3.0f, 0.0f, 4.0f)
                                        : std::clamp(1.0f + parameters.levelEnvAmount * envValue, 0.0f, 2.0f);

        const float f0 = noteToFrequencyHz(modulatedNote) / static_cast<float>(sampleRateHz);
        const float accent = std::clamp(parameters.accent, 0.0f, 1.0f);
        const float structure =
            std::clamp(parameters.harmonics + parameters.harmonicsEnvAmount * envValue, 0.0f, 1.0f);

        harmonicsLp += (structure - harmonicsLp) * 0.01f; // ONE_POLE(harmonics_lp_, parameters.harmonics, 0.01f)

        float brightness = std::clamp(parameters.timbre + parameters.timbreEnvAmount * envValue, 0.0f, 1.0f);
        float damping = std::clamp(parameters.morph + parameters.morphEnvAmount * envValue, 0.0f, 1.0f);
        brightness += 0.25f * accent * (1.0f - brightness);
        damping += 0.25f * accent * (1.0f - damping);

        constexpr float kRange = 60.0f; // non-sustain branch's own fixed constant
        const float f = 2.0f * f0;      // non-sustain branch's own fixed multiplier
        const float cutoff =
            std::min(f * semitonesToRatio((brightness * (2.0f - brightness) - 0.5f) * kRange), 0.499f);
        constexpr float kQ = 1.5f; // non-sustain branch's own fixed Q

        excitationFilter.setFrequencyAndResonance(std::clamp(cutoff, 0.0001f, 0.499f), kQ);

        static thread_local std::vector<float> temp;
        if (temp.size() < numSamples)
            temp.resize(numSamples);
        std::fill(temp.begin(), temp.begin() + static_cast<long>(numSamples), 0.0f);

        if (triggerNow) {
            const float attenuation = 1.0f - damping * 0.5f;
            const float amplitude = (0.12f + 0.08f * accent) * attenuation;
            temp[0] = amplitude * semitonesToRatio(cutoff * cutoff * 24.0f) / std::max(cutoff, 0.0001f);
        }

        // Real bug . Computed here so
        // BOTH aux and out use the exact same scaling - see the two
        // aux bugs fixed just below (same pattern/root-cause as
        // StringEngine's own identical fix, "o string so solta
        // cliques" audit).
        const float outputGain = std::clamp(parameters.accent, 0.0f, 1.0f) * postProcessing.outGain * levelEnvGain;

        for (size_t i = 0; i < numSamples; ++i) {
            temp[i] = excitationFilter.process(temp[i], Svf::Mode::LowPass);
            if (aux) {
                // Two real bugs fixed , same as StringEngine's
                // own identical fix: (1) `+=` accumulated onto a stale
                // thread_local buffer shared across every voice/part
                // (cross-voice bleed once Aux Mix > 0) - now assigns;
                // (2) aux was completely unscaled by outputGain, so the
                // raw excitation click played at a fixed level
                // regardless of accent/level - now scaled to match out.
                aux[i] = temp[i] * outputGain;
            }
        }

        resonator.process(f0, harmonicsLp, brightness, damping, temp.data(), out, numSamples);

        float peakActivity = activity;
        for (size_t i = 0; i < numSamples; ++i) {
            out[i] *= outputGain;
            peakActivity = std::max(peakActivity * 0.9995f, std::abs(out[i]));
        }
        activity = peakActivity;
        if (holdSamplesRemaining > 0)
            holdSamplesRemaining = std::max(0, holdSamplesRemaining - static_cast<int>(numSamples));
        else if (activity < 0.0008f)
            gateOpen = false;
    }

private:
    static float semitonesToRatio(float semitones) { return std::exp2(semitones / 12.0f); }

    double sampleRateHz = 48000.0;
    Resonator resonator;
    Svf excitationFilter;
    DecayEnvelope decayEnvelope;
    float harmonicsLp = 0.0f;
    float activity = 0.0f;
    int holdSamplesRemaining = 0;
    bool gateOpen = false;
};

} //
