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

#include "Svf.h"

#include <algorithm>
#include <cmath>

namespace ruidocore {

// AnalogBassDrum DSP implementation.
// See THIRD_PARTY.md and the preserved source-license notice for attribution.
//
// Keep band-pass and low-pass resonator signals separate. The pulse path
// contains a diode-shaped high-pass term; excitation and resonator gain
// use their respective signal paths.
//
// NOT ported: reference "sustain mode" (a held-gate
// SineOscillator replacing the resonator) - this library's voices are
// always percussive/triggered (see FmEngine/WavetableEngine's own
// identical scope-cut). The `morph` parameter has no role in the real
// engine either (unused there too - not a gap on this port's side).
class AnalogBassDrum {
public:
    void init(double sampleRate) {
        sampleRateHz = sampleRate;
        reset();
    }

    void reset() {
        resonator.reset();
        pulseRemaining = 0;
        fmPulseRemaining = 0;
        pulse = 0.0f;
        pulseHeight = 0.0f;
        pulseLp = 0.0f;
        fmPulseLp = 0.0f;
        retrigPulse = 0.0f;
        toneLp = 0.0f;
        lastResonatorLp = 0.0f;
    }

    void trigger(float accent) {
        pulseRemaining = static_cast<int>(1.0e-3 * sampleRateHz);   // kTriggerPulseDuration, ~1ms
        fmPulseRemaining = static_cast<int>(6.0e-3 * sampleRateHz); // kFMPulseDuration, ~6ms
        pulseHeight = 3.0f + 7.0f * std::clamp(accent, 0.0f, 1.0f); // reference 3..10 "volt" range, kept as-is (just a convenient exciter-amplitude scale, not a literal voltage here)
    }

    // f0: normalized carrier frequency (cycles/sample). decay/tone: 0..1
    // (this library's own EngineParameters convention). attackFmAmount/
    // selfFmAmount: 0..1, both derived from `harmonics` by the caller
    // (see AnalogBassDrumEngine).
    float process(float f0, float decay, float tone, float attackFmAmount, float selfFmAmount) {
        const float pulseDecayTime = static_cast<float>(0.2e-3 * sampleRateHz);   // ~0.2ms
        const float pulseFilterTime = static_cast<float>(0.1e-3 * sampleRateHz);  // ~0.1ms
        const float retrigPulseDuration = static_cast<float>(0.05 * sampleRateHz); // ~50ms

        if (pulseRemaining > 0) {
            --pulseRemaining;
            pulse = pulseRemaining > 0 ? pulseHeight : (pulseHeight - 1.0f); // one-sample step down on the very last tick of the pulse
        } else {
            pulse *= (1.0f - 1.0f / pulseDecayTime);
        }

        float fmPulseValue = 0.0f;
        if (fmPulseRemaining > 0) {
            --fmPulseRemaining;
            fmPulseValue = 1.0f;
            // Held at exactly 0 for the whole FM pulse, then fires -0.8
            // on the very last tick - a brief cancellation that shapes
            // the transient's own tail. Only starts decaying (the else
            // branch below) once the FM pulse has fully ended - the
            // earlier port decayed it every sample regardless, which
            // silently blunted the -0.8 spike before it ever got read.
            retrigPulse = fmPulseRemaining > 0 ? 0.0f : -0.8f;
        } else {
            retrigPulse *= (1.0f - 1.0f / retrigPulseDuration);
        }

        // Diode nonlinearity: asymmetric soft saturation - a smooth knee
        // below zero, a straight pass-through above it.
        const auto diode = [](float x) { return x < 0.0f ? 0.7f * (2.0f * x) / (1.0f + std::abs(2.0f * x)) : x; };

        pulseLp += (pulse - pulseLp) / pulseFilterTime;
        fmPulseLp += (fmPulseValue - fmPulseLp) / pulseFilterTime;

        // `pulse` is REASSIGNED here to a diode-shaped high-pass of
        // itself against its own low-pass, plus a small leak term - this
        // reshaped signal (not the raw exciter pulse, and not pulseLp)
        // is what actually drives both the resonator input and the tone
        // mix below. The earlier port fed pulseLp into the resonator
        // directly and never applied this step at all, silently
        // dropping the click/punch transient reference circuit model
        // actually produces here.
        pulse = diode((pulse - pulseLp) + pulse * 0.044f);

        const float punch = 0.7f + diode(10.0f * lastResonatorLp - 1.0f);
        const float selfFm = punch * 0.08f * selfFmAmount;
        const float attackFm = fmPulseLp * 1.7f * attackFmAmount;
        const float resonatorFrequency = std::clamp(f0 * (1.0f + selfFm + attackFm), 0.0f, 0.4f);

        // Q scaling uses the POST-FM frequency `resonatorFrequency`, not
        // the raw carrier `f0` - matches reference
        // `set_f_q<FREQUENCY_DIRTY>(f, 1.0f + q*f)` exactly (both args
        // are the same post-FM `f`).
        const float q = 1500.0f * std::exp2(decay * 80.0f / 12.0f);
        const float resonanceQ = 1.0f + q * resonatorFrequency;
        resonator.setFrequencyAndResonance(resonatorFrequency, resonanceQ);

        const float scale = 0.001f / std::max(f0, 0.001f);
        const float resonatorInput = (pulse - retrigPulse * 0.2f) * scale;
        // Band-pass and low-pass are DIFFERENT signals used for
        // DIFFERENT purposes in reference, from the same filter pass:
        // band-pass is the audible tone, low-pass feeds back into next
        // sample's punch/self-FM (see Svf::processBandPassAndLowPass).
        float resonatorBp = 0.0f;
        resonator.processBandPassAndLowPass(resonatorInput, resonatorBp, lastResonatorLp);

        const float exciterLeak = 0.08f * (tone + 0.25f);
        const float toneF = std::min(4.0f * f0 * semitonesToRatio(tone * 108.0f), 1.0f);
        toneLp += (pulse * exciterLeak + resonatorBp - toneLp) * toneF;

        return toneLp;
    }

private:
    static float semitonesToRatio(float semitones) { return std::exp2(semitones / 12.0f); }

    double sampleRateHz = 48000.0;
    int pulseRemaining = 0, fmPulseRemaining = 0;
    float pulse = 0.0f, pulseHeight = 0.0f, pulseLp = 0.0f, fmPulseLp = 0.0f;
    float retrigPulse = 0.0f, toneLp = 0.0f, lastResonatorLp = 0.0f;
    Svf resonator;
};

} //
