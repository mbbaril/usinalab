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
#include "../Dsp/CombString.h"
#include "../Dsp/Svf.h"
#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ruidocore {

// StringEngine DSP implementation.
// See THIRD_PARTY.md and the preserved source-license notice for attribution.
//
// Knob mapping confirmed against the manual and the real source:
// harmonics -> `structure` (fed STRAIGHT into a nonlinearity-amount
// formula, not smoothed the way ModalEngine's own harmonics is), timbre
// -> brightness (squared before use - `parameters.timbre *
// parameters.timbre`, reference exact expression, not this
// port's invention), morph -> damping.
//
// One internal string voice per engine instance; the host owns polyphony and
// voice stealing. Aux contains excitation rather than an independent string voice.
//
// NOT ported: reference "sustain mode" (same scope-cut
// as every engine in this library). Already self-enveloping (the
// waveguide's own damping-filter decay IS the amplitude envelope, same
// "internal LPG disabled" reasoning as every other physical-modelling/
// drum engine).
class StringEngine : public Engine {
public:
    StringEngine() { postProcessing.alreadyEnveloped = true; }

    bool isActive() const override { return gateOpen; }
    float getEnvelopeValue01() const override { return decayEnvelope.value(); }

    void init(double sampleRate) override {
        sampleRateHz = sampleRate;
        string_.setSampleRate(sampleRate);
        string_.init();
        excitationFilter.reset();
        reset();
    }

    void reset() override {
        string_.reset();
        decayEnvelope.reset();
        remainingNoiseSamples = 0;
        activity = 0.0f;
        holdSamplesRemaining = 0;
        gateOpen = false;
    }

    void render(const EngineParameters& parameters, const float* /*in*/, float* out, float* aux,
                size_t numSamples) override {
        const bool triggerNow = parameters.trigger == TriggerState::RisingEdge;
        if (triggerNow)
            decayEnvelope.trigger();

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
        const float modulatedTimbre = std::clamp(parameters.timbre + parameters.timbreEnvAmount * envValue, 0.0f, 1.0f);

        float brightness = modulatedTimbre * modulatedTimbre; // real StringEngine::Render's own exact expression
        float damping = std::clamp(parameters.morph + parameters.morphEnvAmount * envValue, 0.0f, 1.0f);
        brightness += 0.25f * accent * (1.0f - brightness);
        damping += 0.25f * accent * (1.0f - damping);

        if (triggerNow) {
            gateOpen = true;
            holdSamplesRemaining = static_cast<int>(2.0 * sampleRateHz);

            constexpr float kRange = 72.0f;
            const float f = 4.0f * f0;
            const float cutoff =
                std::min(f * semitonesToRatio((brightness * (2.0f - brightness) - 0.5f) * kRange), 0.499f);
            constexpr float kQ = 0.5f; // non-sustain branch's own fixed Q
            excitationFilter.setFrequencyAndResonance(std::clamp(cutoff, 0.0001f, 0.499f), kQ);
            remainingNoiseSamples = static_cast<size_t>(1.0f / std::max(f0, 1.0e-6f));
        }

        static thread_local std::vector<float> temp;
        if (temp.size() < numSamples)
            temp.resize(numSamples);

        if (remainingNoiseSamples > 0) {
            const size_t noiseSamples = std::min(remainingNoiseSamples, numSamples);
            remainingNoiseSamples -= noiseSamples;
            for (size_t i = 0; i < noiseSamples; ++i)
                temp[i] = 2.0f * nextNoiseSample01() - 1.0f;
            for (size_t i = noiseSamples; i < numSamples; ++i)
                temp[i] = 0.0f;
        } else {
            std::fill(temp.begin(), temp.begin() + static_cast<long>(numSamples), 0.0f);
        }

        // Apply outputGain to the resonator output for consistent accent and level control.
        const float outputGain = std::clamp(parameters.accent, 0.0f, 1.0f) * postProcessing.outGain * levelEnvGain;

        for (size_t i = 0; i < numSamples; ++i) {
            temp[i] = excitationFilter.process(temp[i], Svf::Mode::LowPass);
            if (aux) {
                // Overwrite aux scratch output rather than accumulating stale samples across voices.
                // Apply outputGain to aux as well as out so accent and level affect both paths.
                aux[i] = temp[i] * outputGain;
            }
        }

        // Real StringVoice::Render's own exact piecewise mapping from
        // `structure` to a bipolar nonlinearity amount - a dead zone
        // around structure=0.25 (pure Karplus-Strong, no curved-bridge
        // or dispersion coloration at all), ramping to -1/+1 at the
        // extremes.
        const float structure = std::clamp(parameters.harmonics + parameters.harmonicsEnvAmount * envValue, 0.0f, 1.0f);
        const float nonLinearity = structure < 0.24f ? (structure - 0.24f) * 4.166f
                                    : (structure > 0.26f) ? (structure - 0.26f) * 1.35135f
                                                           : 0.0f;

        for (size_t i = 0; i < numSamples; ++i)
            out[i] = 0.0f;
        string_.process(f0, nonLinearity, brightness, damping, temp.data(), out, numSamples);

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

    float nextNoiseSample01() {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return static_cast<float>(rngState) / 4294967296.0f;
    }

    double sampleRateHz = 48000.0;
    CombString string_;
    Svf excitationFilter;
    DecayEnvelope decayEnvelope;
    size_t remainingNoiseSamples = 0;
    float activity = 0.0f;
    int holdSamplesRemaining = 0;
    bool gateOpen = false;
    std::uint32_t rngState = 0x85ebca6bu;
};

} //
