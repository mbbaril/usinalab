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
#include "../Dsp/PlaitsString.h"
#include "../Dsp/Svf.h"
#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ruidocore {

// A faithful port of Mutable Instruments' Plaits `StringEngine`+
// `StringVoice` (plaits/dsp/engine/string_engine.h/.cc +
// plaits/dsp/physical_modelling/string_voice.h/.cc, MIT licensed,
// github.com/pichenettes/eurorack, downloaded and read directly - "nao
// simplifique nada"/"quero fidelidade total", 2026-08-27) - "Extended
// Karplus-Strong, with all the niceties from Rings" (real Plaits' own
// doc comment), driving Dsp/PlaitsString.h's waveguide.
//
// Knob mapping confirmed against the manual and the real source:
// harmonics -> `structure` (fed STRAIGHT into a nonlinearity-amount
// formula, not smoothed the way ModalEngine's own harmonics is), timbre
// -> brightness (squared before use - `parameters.timbre *
// parameters.timbre`, real Plaits' own exact expression, not this
// port's invention), morph -> damping.
//
// Real Plaits' own StringEngine renders THREE independent StringVoice
// instances with its own internal round-robin voice-stealing
// (`kNumStrings=3`, `active_string_`) - deliberately NOT replicated
// here: this library's own `PluginProcessor` already gives every part a
// round-robin voice POOL at a higher level (`kVoicesPerPart`), so
// stacking a SECOND independent round-robin inside the engine itself
// would just be redundant polyphony management, not a fidelity loss on
// the actual STRING ALGORITHM - each `StringEngine` instance here is
// one single string voice, matching real Plaits' own `StringVoice`
// exactly; RuidoCore's outer voice pool provides the "3+ voices" real
// Plaits achieves internally.
//
// Deliberately NOT ported: Plaits' own "sustain mode" (same scope-cut
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

        // Internal punch envelope (closing the gap - see
        // BassDrumEngine.h/ModalEngine.h's own comments for the full
        // reasoning; same block-rate cadence as ModalEngine since
        // PlaitsString::process() below also only takes one brightness/
        // damping/nonLinearity value for the whole block).
        const float shortDecay = (200.0f * 24.0f / static_cast<float>(sampleRateHz)) *
                                  semitonesToRatio(-96.0f * parameters.decay);
        decayEnvelope.process(shortDecay * 2.0f);
        const float envValue = decayEnvelope.value();
        const float modulatedNote =
            std::clamp(parameters.note + parameters.pitchEnvAmount * envValue * envValue * 48.0f, 0.0f, 127.0f);
        // Widened on the POSITIVE side (2026-08-28, "o de level ainda parece
        // inefetivo" - the old +-1x symmetric range only reached +6dB at max
        // positive amount, easily masked by this engine's own natural decay;
        // negative already reaches full silence at max amount so stays as-is).
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

        // Real bug (2026-08-28, "honestamente accents nao aparecem
        // funcionar" - same gap as ModalEngine's own, see its identical
        // comment): outputGain never multiplied by parameters.accent.
        // Computed here (moved up, 2026-09-01) so BOTH aux and out use
        // the exact same scaling - see the two bugs fixed just below.
        const float outputGain = std::clamp(parameters.accent, 0.0f, 1.0f) * postProcessing.outGain * levelEnvGain;

        for (size_t i = 0; i < numSamples; ++i) {
            temp[i] = excitationFilter.process(temp[i], Svf::Mode::LowPass);
            if (aux) {
                // Two real bugs fixed 2026-09-01 (user report: "o
                // string so solta cliques" - root-caused via a full
                // engine/aux audit, see reference_engine_envamount_hook_gap
                // memory for the fuller story):
                // 1. `aux[i] += temp[i]` ACCUMULATED onto a stale,
                //    thread_local scratch buffer shared across every
                //    voice/part's own render() call in PluginProcessor
                //    (never cleared between voices) - real cross-voice/
                //    cross-part bleed the moment Aux Mix was raised
                //    above 0, unlike every OTHER engine's own aux
                //    write (`aux[i] = ...`, unconditional overwrite).
                //    Now assigns, matching that same convention.
                // 2. aux was written completely UNSCALED - the same
                //    outputGain (accent/level/outGain) that `out` gets
                //    multiplied by below never touched aux at all, so
                //    the raw excitation/pluck "click" played at a
                //    fixed, unattenuated level regardless of how quiet
                //    the actual resonated tone (out) was - exactly what
                //    would make a String part sound like "only clicks"
                //    once Aux Mix leaned toward aux, or once bug 1's
                //    bleed let unscaled click content reach the mix.
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
    PlaitsString string_;
    Svf excitationFilter;
    DecayEnvelope decayEnvelope;
    size_t remainingNoiseSamples = 0;
    float activity = 0.0f;
    int holdSamplesRemaining = 0;
    bool gateOpen = false;
    std::uint32_t rngState = 0x85ebca6bu;
};

} // namespace ruidocore
