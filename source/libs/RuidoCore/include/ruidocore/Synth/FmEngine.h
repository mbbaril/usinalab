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
#include "../Dsp/Downsampler4x.h"
#include "../Dsp/FmQuantizerTable.h"
#include "../Dsp/LowPassGate.h"
#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ruidocore {

// Two-operator FM synthesis with feedback and shared percussive shaping.
// Source attribution is preserved in the license notice and THIRD_PARTY.md.
//
// Apply the shared LPG inside the engine, driven by decay, to shape sustained
// oscillator output into a percussive voice.
//
// The shared decay envelope drives bipolar pitch, timbre and morph depths.
// A depth of zero leaves the corresponding control unchanged.
class FmEngine : public Engine {
public:
    FmEngine() { postProcessing.alreadyEnveloped = true; }

    bool isActive() const override { return gateOpen; }
    float getEnvelopeValue01() const override { return decayEnvelope.value(); }

    void init(double sampleRate) override {
        sampleRateHz = sampleRate;
        lpg.reset();
        reset();
    }

    void reset() override {
        carrierPhase = 0;
        modulatorPhase = 0;
        subPhase = 0;
        previousCarrierFrequency = 0.001f;
        previousModulatorFrequency = 0.001f;
        previousAmount = 0.0f;
        previousSample = 0.0f;
        carrierDownsampler.reset();
        subDownsampler.reset();
        lpgEnvelope.reset();
        decayEnvelope.reset();
        gateOpen = false;
    }

    void render(const EngineParameters& parameters, const float* /*in*/, float* out, float* aux,
                size_t numSamples) override {
        if (parameters.trigger == TriggerState::RisingEdge) {
            lpgEnvelope.trigger();
            decayEnvelope.trigger();
            gateOpen = true;
        }

        // Same note-space reference FM engine uses for the RATIO
        // mechanism, just measured from THIS library's standard MIDI
        // note numbers instead of reference shifted/embedded-
        // calibrated reference (see class comment).
        const float carrierNote = parameters.note;

        const float carrierFrequencyHzBase = noteToFrequencyHz(carrierNote); // Track LPG attack from the base pitch before pitch-envelope modulation.
        // Higher notes open faster; attack increments are normalized per sample.
        constexpr float kModelBlockSize = 24.0f;
        const float hf = 0.3f; // LPG colour/brightness bleed - not yet wired to its own knob
        const float shortDecayBlock = (200.0f * kModelBlockSize / static_cast<float>(sampleRateHz)) *
                                       semitonesToRatio(-96.0f * parameters.decay);
        const float decayTailBlock = (20.0f * kModelBlockSize / static_cast<float>(sampleRateHz)) *
                                          semitonesToRatio(-72.0f * parameters.decay + 12.0f * hf) -
                                      shortDecayBlock;
        const float shortDecay = shortDecayBlock / kModelBlockSize;
        const float decayTail = decayTailBlock / kModelBlockSize;
        // reference `attack = NoteToFrequency(note) * kBlockSize * 2` -
        // a pitch-tracked attack (higher notes open faster), rescaled the
        // same way.
        const float attack = 2.0f * (carrierFrequencyHzBase / static_cast<float>(sampleRateHz));
        const float outputGain = std::clamp(parameters.accent, 0.0f, 1.0f) * postProcessing.outGain;

        const float oversampledRateHz = static_cast<float>(sampleRateHz) * static_cast<float>(kOversampling);
        constexpr float kMaxUint32 = 4294967296.0f;

        for (size_t i = 0; i < numSamples; ++i) {
            if (!gateOpen) {
                out[i] = 0.0f;
                if (aux)
                    aux[i] = 0.0f;
                continue;
            }

            // Rescale decay coefficients per sample to preserve timing across sample rates.
            decayEnvelope.process(shortDecay * 2.0f);
            const float envValue = decayEnvelope.value();
            const float pitchEnvSemitones = parameters.pitchEnvAmount * envValue * envValue * 48.0f;
            const float sampleCarrierNote = carrierNote + pitchEnvSemitones;
            const float sampleTimbre = std::clamp(parameters.timbre + parameters.timbreEnvAmount * envValue, 0.0f, 1.0f);
            const float sampleMorph = std::clamp(parameters.morph + parameters.morphEnvAmount * envValue, 0.0f, 1.0f);
            const float feedback = 2.0f * sampleMorph - 1.0f; // -1..1
            // Harmonics Env . Sweeps the FM ratio quantizer's
            // OWN lookup position over time, not the resulting semitone
            // offset directly - a wobble through the musically-quantized
            // ratio table, matching how the Harmonics knob itself
            // already works.
            const float sampleHarmonics = std::clamp(parameters.harmonics + parameters.harmonicsEnvAmount * envValue, 0.0f, 1.0f);
            const float ratioSemitones = fmFrequencyQuantizerSemitones(sampleHarmonics);
            const float sampleModulatorNote = sampleCarrierNote + ratioSemitones;

            // hf_taming recomputed every sample (not once per render()
            // call) so it responds to the pitch envelope's own transient
            // spike too, same as reference (which applies CV/envelope
            // modulation to `note` BEFORE the FM engine ever computes
            // its own FM index from it) - a spike into a very high
            // instantaneous pitch backs the FM index off right when it
            // would otherwise risk the harshest aliasing, not just at
            // the note's own static/resting pitch.
            float hfTaming = 1.0f - (sampleModulatorNote - 96.0f) * 0.025f;
            hfTaming = std::clamp(hfTaming, 0.0f, 1.0f);
            hfTaming *= hfTaming;
            const float amount = 2.0f * sampleTimbre * sampleTimbre * hfTaming;

            previousCarrierFrequency = noteToFrequencyHz(sampleCarrierNote);
            previousModulatorFrequency = noteToFrequencyHz(sampleModulatorNote);
            previousAmount = amount;

            const std::uint32_t carrierIncrement =
                static_cast<std::uint32_t>(kMaxUint32 * (previousCarrierFrequency / oversampledRateHz));

            // Negative feedback bends the MODULATOR's own FREQUENCY
            // (via the previous carrier sample); positive feedback
            // injects PHASE directly into the modulator instead - two
            // qualitatively different feedback characters from one
            // bipolar knob's two directions, matching reference
            // fm_engine.cc exactly (both mechanisms target the
            // modulator, never the carrier, in the real algorithm).
            const float phaseFeedback = feedback < 0.0f ? 0.5f * feedback * feedback : 0.0f;
            const float modulatorFeedback = feedback > 0.0f ? 0.25f * feedback * feedback : 0.0f;

            float carrierSample = 0.0f, subSample = 0.0f;
            for (int j = 0; j < kOversampling; ++j) {
                modulatorPhase += static_cast<std::uint32_t>(
                    kMaxUint32 * (previousModulatorFrequency / oversampledRateHz) * (1.0f + previousSample * phaseFeedback));
                carrierPhase += carrierIncrement;
                subPhase += carrierIncrement >> 1;

                const float modulator = sinePm(modulatorPhase, modulatorFeedback * previousSample);
                const float carrier = sinePm(carrierPhase, previousAmount * modulator);
                const float sub = sinePm(subPhase, previousAmount * carrier * 0.25f);
                previousSample += (carrier - previousSample) * 0.05f; // ONE_POLE smoothing, matches reference feedback-source lowpass

                carrierDownsampler.accumulate(j, carrier);
                subDownsampler.accumulate(j, sub);
            }
            carrierSample = carrierDownsampler.read();
            subSample = subDownsampler.read();

            lpgEnvelope.processPing(attack, shortDecay, decayTail, hf);
            const float gated = lpg.process(carrierSample, lpgEnvelope.gain(), lpgEnvelope.frequency(),
                                             lpgEnvelope.hfBleed());

            // Level Env: an extra linear gain trim layered on TOP of the
            // LPG's own nonlinear photoresistive cell amplitude decay, not a
            // replacement for it - reference has nothing like this
            // (no software-only 4th normalled destination), so it's a
            // genuine extra creative control this plugin adds.
            // Widened on the POSITIVE side .
            const float levelEnvGain = parameters.levelEnvAmount >= 0.0f
                                            ? std::clamp(1.0f + parameters.levelEnvAmount * envValue * 3.0f, 0.0f, 4.0f)
                                            : std::clamp(1.0f + parameters.levelEnvAmount * envValue, 0.0f, 2.0f);

            out[i] = gated * outputGain * levelEnvGain;
            if (aux)
                aux[i] = subSample * lpgEnvelope.gain() * outputGain * levelEnvGain; // sub-oscillator, one octave down, same LPG amplitude envelope

            if (lpgEnvelope.gain() < 0.0005f && !lpgEnvelope.isRampingUp())
                gateOpen = false;
        }
    }

private:
    static constexpr int kOversampling = 4;

    // Same SinePM convention as reference sine_oscillator.h: `pm` is a
    // phase offset measured in CYCLES (0..1 = 0..360 degrees), not
    // radians - reimplemented with std::sin directly (this library has
    // no sine LUT) rather than copying reference lookup-table version,
    // since a direct std::sin call is simpler and just as correct for a
    // non-embedded target (no LUT-fetch cost pressure here).
    static float sinePm(std::uint32_t phase, float pm) {
        constexpr float kMaxUint32 = 4294967296.0f;
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float phase01 = static_cast<float>(phase) / kMaxUint32;
        return std::sin(kTwoPi * (phase01 + pm));
    }

    double sampleRateHz = 48000.0;
    std::uint32_t carrierPhase = 0, modulatorPhase = 0, subPhase = 0;
    float previousCarrierFrequency = 0.001f, previousModulatorFrequency = 0.001f;
    float previousAmount = 0.0f;
    float previousSample = 0.0f;
    Downsampler4x carrierDownsampler, subDownsampler;
    LpgEnvelope lpgEnvelope;
    LowPassGate lpg;
    DecayEnvelope decayEnvelope; // see Dsp/DecayEnvelope.h - off (pitchEnvAmount = 0) unless the host explicitly dials it in
    bool gateOpen = false;
};

} //
