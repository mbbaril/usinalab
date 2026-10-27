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

#include "../Dsp/ClockedNoise.h"
#include "../Dsp/DecayEnvelope.h"
#include "../Dsp/LowPassGate.h"
#include "../Dsp/Svf.h"
#include "Engine.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ruidocore {

// A faithful port of Mutable Instruments' Plaits `NoiseEngine`
// (plaits/dsp/engine/noise_engine.h/.cc, "Clocked noise processed by a
// multimode filter", MIT licensed, github.com/pichenettes/eurorack,
// downloaded and read directly). Two ClockedNoise sources (one at the
// carrier pitch, one detuned by the Harmonics-derived interval) feed a
// multimode LP-to-HP filter (out) and two parallel band-pass filters
// (aux) - a percussive, pitched "filtered noise" character rather than
// plain static, distinct from AnalogHiHat's own noise use.
//
// Trigger-vs-CV semantics: real Plaits' `clock_lowest_note` and
// `SwarmEngine`'s own `burst_mode` (see SwarmEngine.h) both branch on
// whether a CV cable is physically PATCHED into the trigger input -
// this library has no such concept (every voice is always internally
// triggered by the sequencer, the software equivalent of "trigger
// always patched"), so this port always takes the "patched" branch
// (`clock_lowest_note = -24.0f`) rather than exposing a knob for
// something with no analogue here.
//
// Percussive envelope: like FmEngine/WavetableEngine, this is a
// continuous/sustained algorithm in real Plaits (used there in
// CV-gated sustain contexts) - shares the same LpgEnvelope+
// LowPassGate+DecayEnvelope machinery, driven by `decay`, block-rate
// updated (kControlBlockSize=24, matching real Plaits' own
// Voice::Render() control-block cadence).
//
// Parameter mapping - identical to real Plaits' own (confirmed from
// source, matches the manual's own "Harmonics: crossfade between LP,
// BP, HP; Timbre: clock frequency; Morph: resonance" description):
//   harmonics -> LP/BP/HP filter mode crossfade
//   timbre    -> clocked-noise frequency
//   morph     -> filter resonance (Q)
//   decay     -> LPG gain/brightness decay time
//   accent    -> output level only
class FilteredNoiseEngine : public Engine {
public:
    FilteredNoiseEngine() { postProcessing.alreadyEnveloped = true; }

    bool isActive() const override { return gateOpen; }
    float getEnvelopeValue01() const override { return decayEnvelope.value(); }

    void init(double sampleRate) override {
        sampleRateHz = sampleRate;
        lpg.reset();
        reset();
    }

    void reset() override {
        noiseA.init();
        noiseB.init();
        lpHpFilter.reset();
        bpFilterA.reset();
        bpFilterB.reset();
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
            // f1: a second pitch, offset by up to +-24 semitones via
            // Harmonics - real Plaits' own `note + harmonics*48 - 24`.
            const float f1 =
                noteToFrequencyHz(parameters.note + pitchEnvSemitones + sampleHarmonics * 48.0f - 24.0f) / static_cast<float>(sampleRateHz);
            constexpr float kClockLowestNote = -24.0f; // "trigger patched" branch - see class comment
            const float clockF =
                noteToFrequencyHz(sampleTimbre * (128.0f - kClockLowestNote) + kClockLowestNote) / static_cast<float>(sampleRateHz);
            const float q = 0.5f * semitonesToRatio(sampleMorph * 120.0f);
            const float gain = 1.0f / std::sqrt((0.5f + q) * 40.0f * std::max(f0, 1e-6f));

            noiseA.render(sync, clockF, scratchA.data(), blockLength);
            noiseB.render(sync, clockF * (f1 / std::max(f0, 1e-6f)), scratchB.data(), blockLength);
            sync = false; // only the first block of a trigger re-syncs the clock phase

            lpHpFilter.setFrequencyAndResonance(f0, q);
            bpFilterA.setFrequencyAndResonance(f0, q);
            bpFilterB.setFrequencyAndResonance(f1, q);

            for (size_t j = 0; j < blockLength; ++j, ++i) {
                const float input1 = scratchA[j] * gain;
                const float input2 = scratchB[j] * gain;

                const float shaped = lpHpFilter.processMultimodeLpToHp(input1, sampleHarmonics);
                const float rawOut = bpFilterA.process(input1, Svf::Mode::BandPass) + bpFilterB.process(input2, Svf::Mode::BandPass);

                const float gated = lpg.process(shaped, lpgEnvelope.gain(), lpgEnvelope.frequency(), lpgEnvelope.hfBleed());
                out[i] = gated * outputGain * levelEnvGain;
                if (aux)
                    aux[i] = rawOut * lpgEnvelope.gain() * outputGain * levelEnvGain;

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

    double sampleRateHz = 48000.0;
    ClockedNoise noiseA, noiseB;
    std::array<float, kControlBlockSize> scratchA{}, scratchB{};
    Svf lpHpFilter, bpFilterA, bpFilterB;
    LpgEnvelope lpgEnvelope;
    LowPassGate lpg;
    DecayEnvelope decayEnvelope;
    bool gateOpen = false;
};

} // namespace ruidocore
