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

#include "Svf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ruidocore {

// A faithful-effort port of Mutable Instruments' Plaits `HiHat`
// (plaits/dsp/drums/hi_hat.h, MIT licensed, github.com/pichenettes/
// eurorack), specifically the `hi_hat_1_` configuration
// (`HiHat<SquareNoise, SwingVCA, resonance=true, twoStageEnvelope=false>`)
// real Plaits itself uses for its own `out` channel - six square-wave
// oscillators at fixed inharmonic ratios (a classic 808/909-style
// "metallic" cluster) crossfaded against clocked noise, shaped by a
// resonant band-pass ("tone") and a decay envelope, finished with a
// high-pass filter. Knob mapping confirmed against the official Plaits
// manual, per this project's own standing practice (see
// AnalogSnareDrum.h's own header comment): Harmonics="balance of the
// metallic and filtered noise" -> `noisiness`, Timbre="high-pass filter
// cutoff" -> `tone`, Morph="decay time" -> `decay`.
//
// Fidelity re-review (2026-08-27, "faça o mesmo método de baixar o
// código" - direct source download resolved every gap the earlier
// researched port had disclosed, and found more besides):
// (1) the metallic cluster's 6 oscillators run at `2.0f * f0`, not
// `f0` - the earlier port was missing this doubling entirely;
// (2) the coloration BPF's resonance is `3.0f + 3.0f*tone` for this
// resonance=true config, not the earlier port's guessed `1.0f + 5.0f*
// tone`; (3) `noisiness` is squared BEFORE both its uses - the earlier
// port only squared it for the noise-clock rate, but real Plaits also
// uses the SAME squared value for the metallic/noise crossfade blend,
// not the raw knob value; (4) `SwingVCA`'s real formula is resolved
// exactly now: `s *= (s>0 ? 4.0f : 0.1f); s = s/(1+|s|); return
// (s+0.1f)*gain` - an ASYMMETRIC pre-scale on the raw signal BEFORE
// saturating, then a `+0.1f` DC offset added AFTER saturating but
// BEFORE the envelope-gain multiply - qualitatively different from the
// earlier port's gain-then-saturate approximation, not just re-tuned
// constants. The BPF's own 6-octave/72-semitone tone sweep and the
// 16000Hz upper cutoff clamp are both confirmed exact by the real
// source too.
class AnalogHiHat {
public:
    void init(double sampleRate) {
        sampleRateHz = sampleRate;
        reset();
    }

    void reset() {
        for (auto& p : oscPhases)
            p = 0;
        toneFilter.reset();
        hpf.reset();
        noiseClockPhase = 0.0f;
        noiseSample = 0.0f;
        envelope = 0.0f;
        rngState = 0x9e3779b9u;
    }

    void trigger(float decay, float accent) {
        envelope = (1.5f + 0.5f * (1.0f - decay)) * (0.3f + 0.7f * std::clamp(accent, 0.0f, 1.0f));
    }

    // tone/noisiness/decay: 0..1 (see class comment for the knob
    // mapping this port assigns).
    float process(float f0, float tone, float noisiness, float decay) {
        // Six square oscillators at Plaits' own documented inharmonic
        // ratios (nominal reference 414Hz), summed to a -1..1 "metallic"
        // signal.
        static constexpr float kRatios[6] = {1.0f, 1.304f, 1.466f, 1.787f, 1.932f, 2.536f};
        const float metallicF0 = 2.0f * f0; // real Plaits' own Render(2.0f * f0, ...) call - the earlier port was missing this doubling
        int litCount = 0;
        for (int i = 0; i < 6; ++i) {
            const float oscFrequency = std::clamp(metallicF0 * kRatios[i], 0.0f, 0.499f);
            const auto increment = static_cast<std::uint32_t>(oscFrequency * 4294967296.0);
            oscPhases[static_cast<size_t>(i)] += increment;
            if (oscPhases[static_cast<size_t>(i)] & 0x80000000u)
                ++litCount;
        }
        const float metallic = 0.33f * static_cast<float>(litCount) - 1.0f;

        const float toneRatio = semitonesToRatio(tone * 72.0f); // 6-octave/72-semitone sweep, confirmed exact
        const float nyquistCap = 16000.0f / static_cast<float>(sampleRateHz); // real Plaits' own upper clamp, not a generic 0.499 Nyquist cap
        const float bpfFrequency = std::clamp((150.0f * toneRatio) / static_cast<float>(sampleRateHz), 0.0f, nyquistCap);
        toneFilter.setFrequencyAndResonance(bpfFrequency, 3.0f + 3.0f * tone); // resonance=true config's real Q formula
        float out = toneFilter.process(metallic, Svf::Mode::BandPass);

        // Clocked random sampler ("noisiness" crossfade source) - a
        // held random value, resampled at a rate that itself depends on
        // noisiness and pitch.
        // `f0` is ALREADY normalized (cycles/sample) - dividing by
        // sampleRateHz again here (an earlier version of this file did)
        // made the clock run ~48000x too slow, so noiseSample almost
        // never updated within a hit's own lifetime and every noisiness
        // setting just attenuated the metallic signal toward silence
        // instead of ever crossfading in real noise - caught from a
        // real by-ear test, "para a direita nao tenho som" (user
        // scrolling toward high Harmonics/noisiness and getting total
        // silence at the very top of the range, since metallic had
        // faded out and noise never actually arrived).
        const float noisinessSquared = noisiness * noisiness;
        const float noiseClockFrequency = std::clamp(f0 * (16.0f + 16.0f * (1.0f - noisinessSquared)), 0.0f, 0.5f);
        noiseClockPhase += noiseClockFrequency;
        if (noiseClockPhase >= 1.0f) {
            noiseClockPhase -= 1.0f;
            noiseSample = nextNoiseSample();
        }
        // Crossfade blend also uses the SQUARED noisiness (the same
        // reassigned value the clock-rate formula above uses), not the
        // raw knob value - real Plaits reassigns its local `noisiness`
        // to its own square once, then reads it twice.
        out += noisinessSquared * (noiseSample - out);

        const float decayCoefficient = 1.0f - 0.003f * semitonesToRatio(-decay * 84.0f); // 1-0.003*2^(-decay*7), semitonesToRatio(-decay*84)=2^(-decay*7)
        envelope *= decayCoefficient;

        // SwingVCA, exact: asymmetric pre-scale on the RAW (pre-gain)
        // signal, soft-saturate, THEN add a +0.1 DC offset, THEN
        // multiply by the envelope gain - in that order.
        float swung = out * (out > 0.0f ? 4.0f : 0.1f);
        swung = swung / (1.0f + std::abs(swung));
        out = (swung + 0.1f) * envelope;

        // HPF: same tone-derived cutoff the BPF uses, but its OWN Svf
        // instance/state and a fixed Q=0.5 (per the researched formula -
        // not the tone-scaled resonance the BPF above uses).
        hpf.setFrequencyAndResonance(bpfFrequency, 0.5f);
        out = hpf.process(out, Svf::Mode::HighPass);

        return out;
    }

private:
    static float semitonesToRatio(float semitones) { return std::exp2(semitones / 12.0f); }

    float nextNoiseSample() {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return (static_cast<float>(rngState) / 4294967295.0f) * 2.0f - 1.0f;
    }

    double sampleRateHz = 48000.0;
    std::array<std::uint32_t, 6> oscPhases{};
    Svf toneFilter, hpf;
    float noiseClockPhase = 0.0f, noiseSample = 0.0f;
    float envelope = 0.0f;
    std::uint32_t rngState = 0x9e3779b9u;
};

} // namespace ruidocore
