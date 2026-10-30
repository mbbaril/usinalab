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
#include "../Dsp/LowPassGate.h"
#include "../Dsp/WaveBankData.h"
#include "Engine.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ruidocore {

// Read the waveform terrain with trilinear bank interpolation and Hermite phase
// interpolation. Differentiate integrated waveforms for antialiasing.
// Source attribution is preserved in the license notice and THIRD_PARTY.md.
//
// Output follows the differentiator directly; do not add a separate SVF low-pass stage.
//
// Percussive envelope + pitch/timbre/morph "punch" : shares the exact same
// `LpgEnvelope`+`LowPassGate` (Dsp/LowPassGate.h) and `DecayEnvelope`
// (Dsp/DecayEnvelope.h) modules FmEngine.h uses, driven by the SAME
// per-block decay-time formula from reference `Voice::Render`
// (the reference DSP source) - reference decay-time formula is Voice-
// level, shared across every engine, not engine-specific, so this isn't
// a shortcut, it's what makes both engines' Decay knob feel consistent.
//
// Update pitch, timbre, morph and envelope modulation once per 24-sample control
// block. Oscillator phase and waveform interpolation still advance per sample.
//
// Parameter mapping - IDENTICAL to reference (not
// reinterpreted differently), since the X/Y/Z terrain read only makes
// sense with this exact assignment:
// timbre -> X position across the 8-wide row (0..7)
// morph -> Y position across the 8 rows within one bank (0..7)
// harmonics -> Z position across the 4 banks (0..3, folded/mirrored -
// see wave_map_ construction below, matching reference
// own fold trick exactly) - NOT modulated by the internal
// envelope, matching reference (only FM/TIMBRE/MORPH
// are normalled to it, never the bank-select axis).
// decay -> LPG gain/brightness decay time (see LowPassGate.h)
// accent -> output level only
class WavetableEngine : public Engine {
public:
    WavetableEngine() {
        postProcessing.alreadyEnveloped = true;
        buildWaveMap();
    }

    bool isActive() const override { return gateOpen; }
    float getEnvelopeValue01() const override { return decayEnvelope.value(); }

    void init(double sampleRate) override {
        sampleRateHz = sampleRate;
        lpg.reset();
        reset();
    }

    void reset() override {
        phase01 = 0.0f;
        gateOpen = false;
        differentiatorPrevious = 0.0f;
        differentiatorLp = 0.0f;
        lpgEnvelope.reset();
        decayEnvelope.reset();
    }

    void render(const EngineParameters& parameters, const float* /*in*/, float* out, float* aux,
                size_t numSamples) override {
        if (parameters.trigger == TriggerState::RisingEdge) {
            phase01 = 0.0f;
            lpgEnvelope.trigger();
            decayEnvelope.trigger();
            gateOpen = true;
        }

        const float outputGain = std::clamp(parameters.accent, 0.0f, 1.0f) * postProcessing.outGain;
        const float hf = 0.3f; // LPG colour/brightness bleed - not yet wired to its own knob, same as FmEngine

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

            // Everything below runs ONCE per control block, matching
            // reference Voice::Render()/kBlockSize cadence (see
            // class comment) - this block's own LPGEnvelope/
            // DecayEnvelope update, the resulting pitch/timbre/morph
            // "punch," and the X/Y/Z terrain blend indices derived from
            // it.
            const float shortDecay =
                (200.0f * kControlBlockSize / static_cast<float>(sampleRateHz)) * semitonesToRatio(-96.0f * parameters.decay);
            const float decayTail = (20.0f * kControlBlockSize / static_cast<float>(sampleRateHz)) *
                                         semitonesToRatio(-72.0f * parameters.decay + 12.0f * hf) -
                                     shortDecay;
            const float baseFrequencyHzForAttack = noteToFrequencyHz(parameters.note);
            const float attack = baseFrequencyHzForAttack / static_cast<float>(sampleRateHz) * kControlBlockSize * 2.0f;
            lpgEnvelope.processPing(attack, shortDecay, decayTail, hf);

            decayEnvelope.process(shortDecay * 2.0f);
            const float envValue = decayEnvelope.value();
            const float pitchEnvSemitones = parameters.pitchEnvAmount * envValue * envValue * 48.0f;
            const float sampleTimbre = std::clamp(parameters.timbre + parameters.timbreEnvAmount * envValue, 0.0f, 1.0f);
            const float sampleMorph = std::clamp(parameters.morph + parameters.morphEnvAmount * envValue, 0.0f, 1.0f);
            // Harmonics Env - sweeps which bank the Z axis
            // reads from over the hit's own decay.
            const float sampleHarmonics = std::clamp(parameters.harmonics + parameters.harmonicsEnvAmount * envValue, 0.0f, 1.0f);
            // Level Env: a linear gain trim on top of the LPG's own
            // amplitude decay, held fixed for this control block - same
            // reasoning as FmEngine's own levelEnvAmount application.
            // Widened on the POSITIVE side .
            const float levelEnvGain = parameters.levelEnvAmount >= 0.0f
                                            ? std::clamp(1.0f + parameters.levelEnvAmount * envValue * 3.0f, 0.0f, 4.0f)
                                            : std::clamp(1.0f + parameters.levelEnvAmount * envValue, 0.0f, 2.0f);

            const float baseFrequencyHz = noteToFrequencyHz(parameters.note + pitchEnvSemitones);

            // Same X/Y/Z assignment and 6.9999 scaling as reference
            // Render() - 6.9999 (not 7.0) keeps the integral part from
            // ever reaching 7 for the fractional blend's +1 neighbour to
            // stay in range.
            const float xf = sampleTimbre * 6.9999f;
            const float yf = sampleMorph * 6.9999f;
            const float zf = sampleHarmonics * 6.9999f;
            const int x0 = static_cast<int>(xf);
            const int x1 = x0 + 1;
            const float xFrac = xf - static_cast<float>(x0);
            const int y0 = static_cast<int>(yf);
            const int y1 = y0 + 1;
            const float yFrac = yf - static_cast<float>(y0);
            int z0 = static_cast<int>(zf);
            int z1 = z0 + 1;
            const float zFrac = zf - static_cast<float>(z0);
            // The fold: Z nominally spans 0..7 (matching X/Y's own
            // 8-wide range) but only 4 real banks exist, so the upper
            // half mirrors the lower half back (7->0, 6->1, 5->2, 4->3)
            // instead of wrapping - a smooth "there and back" traversal
            // across one knob sweep instead of an abrupt jump at the
            // wrap point.
            if (z0 >= 4)
                z0 = 7 - z0;
            if (z1 >= 4)
                z1 = 7 - z1;

            const float f0 = baseFrequencyHz / static_cast<float>(sampleRateHz); // cycles/sample, held fixed for this whole block

            for (size_t j = 0; j < blockLength; ++j, ++i) {
                phase01 += f0;
                if (phase01 >= 1.0f)
                    phase01 -= 1.0f;

                const float p = phase01 * static_cast<float>(kWaveBankTableSize);
                const int pInt = static_cast<int>(p);
                const float pFrac = p - static_cast<float>(pInt);

                const float xy0z0 = lerp(readWave(x0, y0, z0, pInt, pFrac), readWave(x1, y0, z0, pInt, pFrac), xFrac);
                const float xy1z0 = lerp(readWave(x0, y1, z0, pInt, pFrac), readWave(x1, y1, z0, pInt, pFrac), xFrac);
                const float xyz0 = lerp(xy0z0, xy1z0, yFrac);

                const float xy0z1 = lerp(readWave(x0, y0, z1, pInt, pFrac), readWave(x1, y0, z1, pInt, pFrac), xFrac);
                const float xy1z1 = lerp(readWave(x0, y1, z1, pInt, pFrac), readWave(x1, y1, z1, pInt, pFrac), xFrac);
                const float xyz1 = lerp(xy0z1, xy1z1, yFrac);

                float mix = lerp(xyz0, xyz1, zFrac);

                // Differentiator-based anti-aliasing (reference trick,
                // reused, instead of this library's usual
                // oversample+FIR-decimate - a wavetable read benefits
                // from a DIFFERENT AA approach than a phase-modulation
                // oscillator does): a one-pole highpass-ish leak whose
                // cutoff tracks pitch, plus a gain compensation term,
                // together keep high notes from reading as harsh/
                // aliased without ever oversampling the table read
                // itself.
                const float gain = (1.0f / (f0 * 131072.0f)) * (0.95f - f0);
                const float cutoff = std::min(static_cast<float>(kWaveBankTableSize) * f0, 1.0f);
                differentiatorLp += ((mix - differentiatorPrevious) - differentiatorLp) * cutoff;
                differentiatorPrevious = mix;
                mix = differentiatorLp * gain;

                const float gated = lpg.process(mix, lpgEnvelope.gain(), lpgEnvelope.frequency(), lpgEnvelope.hfBleed());

                out[i] = gated * outputGain * levelEnvGain;
                if (aux)
                    aux[i] = mix * lpgEnvelope.gain() * outputGain * levelEnvGain; // pre-filter terrain read, same LPG amplitude envelope

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
    static constexpr int kControlBlockSize = 24; // matches reference kBlockSize exactly

    static float lerp(float a, float b, float t) { return a + (b - a) * t; }

    // 4th-order Hermite interpolation across one wave's own phase axis -
    // needs the sample BEFORE and the two samples AFTER the interpolation
    // point, which is exactly why each stored wave carries 4 guard
    // samples past its own 128 (see the corresponding DSP header) - ported
    // from reference InterpolateWaveHermite.
    static float readWave(int x, int y, int z, int phaseIntegral, float phaseFractional) {
        const int waveIndex = x + y * 8 + z * 64;
        const int16_t* table =
            waveMap[static_cast<size_t>(std::clamp(waveIndex, 0, kWaveBankCount * 4 - 1))];
        const float xm1 = static_cast<float>(table[phaseIntegral]);
        const float x0 = static_cast<float>(table[phaseIntegral + 1]);
        const float x1 = static_cast<float>(table[phaseIntegral + 2]);
        const float x2 = static_cast<float>(table[phaseIntegral + 3]);
        const float c = (x1 - xm1) * 0.5f;
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + (x2 - x0) * 0.5f;
        const float bNeg = w + a;
        const float f = phaseFractional;
        return (((a * f) - bNeg) * f + c) * f + x0;
    }

    // 4 banks x 64 waves/bank (8x8 each) = 256 slots. Banks 0-2 map
    // directly onto the 192 real waveforms; bank 3 (reference
    // "user-loadable" bank, unused here - no custom wave loading in this
    // v1) falls back to the exact same reindexing formula reference itself
    // uses when no custom data is loaded (`w = i * 101 % kNumWaves`) -
    // matching reference DEFAULT behavior , not inventing a
    // different fallback.
    static void buildWaveMap() {
        static bool built = false;
        if (built)
            return;
        built = true;
        for (int i = 0; i < 4 * 64; ++i) {
            int w = i;
            if (i >= 3 * 64)
                w = (i * 101) % kWaveBankCount;
            waveMap[static_cast<size_t>(i)] = wav_integrated_waves + static_cast<size_t>(w) * kWaveBankStride;
        }
    }

    static inline std::array<const int16_t*, 4 * 64> waveMap{};

    double sampleRateHz = 48000.0;
    float phase01 = 0.0f;
    bool gateOpen = false;
    float differentiatorPrevious = 0.0f;
    float differentiatorLp = 0.0f;
    LpgEnvelope lpgEnvelope;
    LowPassGate lpg;
    DecayEnvelope decayEnvelope;
};

} //
