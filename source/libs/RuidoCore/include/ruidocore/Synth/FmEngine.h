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
#include "../Dsp/Downsampler4x.h"
#include "../Dsp/FmQuantizerTable.h"
#include "../Dsp/LowPassGate.h"
#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ruidocore {

// Classic 2-operator FM voice - a FAITHFUL port of Mutable Instruments'
// Plaits `FMEngine` (plaits/dsp/engine/fm_engine.h/.cc, "Classic 2-op FM,
// as found in Braids, Rings and Elements" - MIT licensed,
// github.com/pichenettes/eurorack, read for this library's own synth-
// engine research, see docs/SYNTH_ENGINE_RESEARCH.md), replacing an
// earlier from-scratch implementation per the user's explicit request
// (2026-08-27, "eu não vejo problema em criar uma drum machine
// totalmente validada" - validated meaning matching Mutable's own
// proven, battle-tested algorithm, not an independently-derived one).
// Kept faithful on the actual oscillator/feedback/anti-aliasing math;
// adapted only where Plaits' own embedded-specific pitch-reference
// calibration (`NoteToFrequency`'s own `a0`/-9-semitone/-24-semitone
// offsets, tuned to their own hardware's exact oscillator range) doesn't
// apply here - this engine uses standard MIDI-note-numbered pitch (see
// Engine.h's own noteToFrequencyHz) throughout instead, while preserving
// the actual musically-quantized-ratio MECHANISM unchanged (same LUT,
// same interpolation).
//
// Percussive envelope: Plaits' own engines are SUSTAINED voices - the
// percussive shaping (an LPG driven by the voice's own Decay knob) lives
// in Plaits' `Voice` class, one level up, not inside FMEngine itself.
// This library's first host is a drum machine, so that LPG (see
// Dsp/LowPassGate.h, also ported faithfully and kept as its own shared
// module so any future voice can reuse it) is applied HERE instead,
// driven by this engine's own `decay` parameter.
//
// Pitch envelope: also real Plaits Voice-level behavior, not invented
// here - the SAME per-hit decay envelope that drives the LPG is, on real
// hardware, ALSO normalled to the FM/TIMBRE/MORPH CV inputs whenever
// nothing else is patched into them, giving a free built-in pitch/
// timbre "punch." Ported as `pitchEnvAmount` (see Engine.h,
// Dsp/DecayEnvelope.h) - a bipolar depth knob, 0 by default (matching
// real hardware's own resting-attenuverter-at-noon = no effect), NOT a
// fixed/fabricated pitch sweep (an earlier from-scratch version of this
// file had exactly that hack, unconditionally applied and uncontrollable
// - removed when this file was rewritten as a faithful port; this is
// its real, host-controllable replacement).
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

        // Same note-space Plaits' own FM engine uses for the RATIO
        // mechanism, just measured from THIS library's standard MIDI
        // note numbers instead of Plaits' own shifted/embedded-
        // calibrated reference (see class comment).
        const float carrierNote = parameters.note;

        const float carrierFrequencyHzBase = noteToFrequencyHz(carrierNote); // pre-pitch-envelope, only used for the LPG's own pitch-tracked attack below

        // LPG shaping - ported from Plaits' own `Voice::Render`
        // (plaits/dsp/voice.cc), which computes these once per its
        // 24-sample control block (`kBlockSize`) and calls
        // LPGEnvelope::ProcessPing once per block. This engine instead
        // updates the envelope every single output SAMPLE (see the
        // per-`i` call below) for smoother continuous shaping instead of
        // Plaits' own stepped-every-24-samples update, so each of
        // Plaits' block-rate coefficients is rescaled by 1/kBlockSize -
        // valid to first order since these are small per-step
        // integrator coefficients, and it preserves the real-world
        // attack/decay TIME regardless of update rate. (An earlier
        // version of this file used fixed made-up coefficients here,
        // producing a near-instant open+close - an audible click instead
        // of a percussive envelope - caught from a real by-ear test.)
        constexpr float kPlaitsBlockSize = 24.0f;
        const float hf = 0.3f; // LPG colour/brightness bleed - not yet wired to its own knob
        const float shortDecayBlock = (200.0f * kPlaitsBlockSize / static_cast<float>(sampleRateHz)) *
                                       semitonesToRatio(-96.0f * parameters.decay);
        const float decayTailBlock = (20.0f * kPlaitsBlockSize / static_cast<float>(sampleRateHz)) *
                                          semitonesToRatio(-72.0f * parameters.decay + 12.0f * hf) -
                                      shortDecayBlock;
        const float shortDecay = shortDecayBlock / kPlaitsBlockSize;
        const float decayTail = decayTailBlock / kPlaitsBlockSize;
        // Plaits' own `attack = NoteToFrequency(note) * kBlockSize * 2` -
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

            // Internal decay envelope (2026-08-27, ported from Plaits'
            // own Voice::Render - see Dsp/DecayEnvelope.h's own
            // comment): the SAME per-sample-rescaled shortDecay
            // coefficient the LPG uses above, but at DOUBLE the rate
            // (`short_decay * 2.0f`, Plaits' own constant) - meant to be
            // a fast transient "punch," snappier than the LPG's own
            // amplitude decay. ONE envelope, normalled to FIVE
            // destinations simultaneously (real Plaits only does
            // pitch/timbre/morph, a hardware CV-input limit; "não temos
            // essa limitação aqui. pode adicionar aos outros" extends it
            // to Harmonics and Level too, since this is software):
            // pitch (squared + scaled to 48 semitones - a sharp spike,
            // not a linear ramp), timbre/morph/harmonics (direct/
            // unsquared, matching Plaits' own simpler formula), and
            // level (a linear gain trim, applied after the LPG below).
            decayEnvelope.process(shortDecay * 2.0f);
            const float envValue = decayEnvelope.value();
            const float pitchEnvSemitones = parameters.pitchEnvAmount * envValue * envValue * 48.0f;
            const float sampleCarrierNote = carrierNote + pitchEnvSemitones;
            const float sampleTimbre = std::clamp(parameters.timbre + parameters.timbreEnvAmount * envValue, 0.0f, 1.0f);
            const float sampleMorph = std::clamp(parameters.morph + parameters.morphEnvAmount * envValue, 0.0f, 1.0f);
            const float feedback = 2.0f * sampleMorph - 1.0f; // -1..1

            // Harmonics Env (2026-08-27, "não temos essa limitação aqui.
            // pode adicionar aos outros" - real Plaits never modulates
            // Harmonics via its internal envelope, since it has no CV
            // input for it at all; software has no such constraint, so
            // it's exposed here too). Sweeps the FM ratio quantizer's
            // OWN lookup position over time, not the resulting semitone
            // offset directly - a wobble through the musically-quantized
            // ratio table, matching how the Harmonics knob itself
            // already works.
            const float sampleHarmonics = std::clamp(parameters.harmonics + parameters.harmonicsEnvAmount * envValue, 0.0f, 1.0f);
            const float ratioSemitones = fmFrequencyQuantizerSemitones(sampleHarmonics);
            const float sampleModulatorNote = sampleCarrierNote + ratioSemitones;

            // hf_taming recomputed every sample (not once per render()
            // call) so it responds to the pitch envelope's own transient
            // spike too, same as real Plaits (which applies CV/envelope
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
            // bipolar knob's two directions, matching Plaits' own
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
                previousSample += (carrier - previousSample) * 0.05f; // ONE_POLE smoothing, matches Plaits' own feedback-source lowpass

                carrierDownsampler.accumulate(j, carrier);
                subDownsampler.accumulate(j, sub);
            }
            carrierSample = carrierDownsampler.read();
            subSample = subDownsampler.read();

            lpgEnvelope.processPing(attack, shortDecay, decayTail, hf);
            const float gated = lpg.process(carrierSample, lpgEnvelope.gain(), lpgEnvelope.frequency(),
                                             lpgEnvelope.hfBleed());

            // Level Env: an extra linear gain trim layered on TOP of the
            // LPG's own nonlinear vactrol amplitude decay, not a
            // replacement for it - real Plaits has nothing like this
            // (no software-only 4th normalled destination), so it's a
            // genuine extra creative control this plugin adds.
            // Widened on the POSITIVE side (2026-08-28, "o de level ainda parece
            // inefetivo" - the old +-1x symmetric range only reached +6dB at max
            // positive amount, easily masked by this engine's own natural decay;
            // negative already reaches full silence at max amount so stays as-is).
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

    // Same SinePM convention as Plaits' own sine_oscillator.h: `pm` is a
    // phase offset measured in CYCLES (0..1 = 0..360 degrees), not
    // radians - reimplemented with std::sin directly (this library has
    // no sine LUT) rather than copying Plaits' own lookup-table version,
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

} // namespace ruidocore
