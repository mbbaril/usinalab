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

#include "DcBlocker.h"
#include "DelayLine.h"
#include "PlaitsResonatorTables.h"
#include "Svf.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ruidocore {

// A faithful port of Mutable Instruments' Plaits `String`
// (plaits/dsp/physical_modelling/string.h/.cc, MIT licensed,
// github.com/pichenettes/eurorack, downloaded and read directly - "nao
// simplifique nada"/"quero fidelidade total", 2026-08-27). Real Plaits'
// own doc comment: "Comb filter / KS string. 'Lite' version of the
// implementation used in Rings" - an extended Karplus-Strong waveguide
// (delay line + damping filter in the feedback loop), NOT a modal bank
// like Dsp/Resonator.h - a genuinely different synthesis technique, kept
// as its own class per real Plaits' own architecture.
//
// Two selectable nonlinearities (real Plaits' own `StringNonLinearity`
// enum, chosen by the sign of `nonLinearityAmount`): a "curved bridge"
// feedback saturation (negative amount) or a dispersion allpass filter
// that stretches high partials out of tune, modeling string stiffness
// (positive amount) - both ported exactly, including the fractional-
// delay Hermite/allpass read logic and the "shitty linear interpolation
// upsampler" real Plaits itself uses for pitches too low to fit the
// 1024-sample delay line (kept verbatim, not simplified away, despite
// its own self-deprecating name in the original source).
class PlaitsString {
public:
    void init() { reset(); }

    void reset() {
        string_.reset();
        stretch_.reset();
        dampingFilter.reset();
        dcBlocker.init(1.0f - 20.0f / static_cast<float>(sampleRateHz));
        dispersionNoise = 0.0f;
        curvedBridge = 0.0f;
        outSample[0] = outSample[1] = 0.0f;
        srcPhase = 0.0f;
        delay_ = 100.0f;
        rngState = 0x2545f491u;
    }

    void setSampleRate(double sr) {
        sampleRateHz = sr;
        dcBlocker.init(1.0f - 20.0f / static_cast<float>(sampleRateHz));
    }

    // nonLinearityAmount: bipolar - negative selects the curved-bridge
    // nonlinearity (with |amount| as its depth), positive selects
    // dispersion (with amount as its depth), matching real Plaits'
    // String::Process's own dispatch exactly.
    void process(float f0, float nonLinearityAmount, float brightness, float damping, const float* in, float* out,
                 size_t size) {
        if (nonLinearityAmount <= 0.0f)
            processInternal<false>(f0, -nonLinearityAmount, brightness, damping, in, out, size);
        else
            processInternal<true>(f0, nonLinearityAmount, brightness, damping, in, out, size);
    }

private:
    static constexpr size_t kDelayLineSize = 1024;

    static float semitonesToRatio(float semitones) { return std::exp2(semitones / 12.0f); }
    static float crossfade(float a, float b, float fade) { return a + (b - a) * fade; }

    float nextNoiseSample01() { // matches stmlib::Random::GetFloat() - uniform [0,1)
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return static_cast<float>(rngState) / 4294967296.0f;
    }

    // isDispersion: compile-time selects which of the two nonlinearity
    // branches runs - real Plaits' own `ProcessInternal<non_linearity>`
    // template parameter, kept as a template here too so both branches
    // stay exactly as faithful/branch-free as the original.
    template <bool isDispersion>
    void processInternal(float f0, float nonLinearityAmount, float brightness, float damping, const float* in,
                          float* out, size_t size) {
        float delay = 1.0f / f0;
        delay = std::clamp(delay, 4.0f, static_cast<float>(kDelayLineSize) - 4.0f);

        float srcRatio = delay * f0;
        if (srcRatio >= 0.9999f) {
            srcPhase = 1.0f;
            srcRatio = 1.0f;
        }

        float dampingCutoff = std::min(12.0f + damping * damping * 60.0f + brightness * 24.0f, 84.0f);
        float dampingF = std::min(f0 * semitonesToRatio(dampingCutoff), 0.499f);

        if (damping >= 0.95f) {
            const float toInfinite = 20.0f * (damping - 0.95f);
            brightness += toInfinite * (1.0f - brightness);
            dampingF += toInfinite * (0.4999f - dampingF);
            dampingCutoff += toInfinite * (128.0f - dampingCutoff);
        }

        dampingFilter.setFrequencyAndResonance(std::clamp(dampingF, 0.0001f, 0.499f), 0.5f);

        const float dampingCompensation = interpolate(kSvfShiftTable, dampingCutoff, 1.0f);

        // Linear interpolation of delay_ toward its new target across
        // this block - real Plaits' own `ParameterInterpolator`,
        // reduced to a plain per-sample lerp here (this library's
        // established simplification level for block-rate parameter
        // smoothing, same as every other engine already ported).
        const float delayTarget = delay * dampingCompensation;
        const float delayStart = delay_;

        const float stretchPoint = nonLinearityAmount * (2.0f - nonLinearityAmount) * 0.225f;
        float stretchCorrection = (160.0f / static_cast<float>(sampleRateHz)) * delay;
        stretchCorrection = std::clamp(stretchCorrection, 1.0f, 2.1f);

        const float noiseAmountSqrt = nonLinearityAmount > 0.75f ? 4.0f * (nonLinearityAmount - 0.75f) : 0.0f;
        const float noiseAmount = noiseAmountSqrt * noiseAmountSqrt * 0.1f;
        const float noiseFilterCoeff = 0.06f + 0.94f * brightness * brightness;

        const float bridgeCurvingSqrt = nonLinearityAmount;
        const float bridgeCurving = bridgeCurvingSqrt * bridgeCurvingSqrt * 0.01f;

        const float apGain = -0.618f * nonLinearityAmount / (0.15f + std::abs(nonLinearityAmount));

        size_t processed = 0;
        for (size_t n = 0; n < size; ++n) {
            srcPhase += srcRatio;
            if (srcPhase > 1.0f) {
                srcPhase -= 1.0f;

                const float frac = static_cast<float>(processed) / static_cast<float>(std::max<size_t>(1, size));
                float d = crossfade(delayStart, delayTarget, frac);
                ++processed;
                float s = 0.0f;

                if constexpr (isDispersion) {
                    const float noise = nextNoiseSample01() - 0.5f;
                    dispersionNoise += (noise - dispersionNoise) * noiseFilterCoeff; // ONE_POLE
                    d *= 1.0f + dispersionNoise * noiseAmount;
                } else {
                    d *= 1.0f - curvedBridge * bridgeCurving;
                }

                if constexpr (isDispersion) {
                    const float apDelay = d * stretchPoint;
                    const float mainDelay = d - apDelay * (0.408f - stretchPoint * 0.308f) * stretchCorrection;
                    if (apDelay >= 4.0f && mainDelay >= 4.0f) {
                        s = string_.read(mainDelay);
                        s = stretch_.allpass(s, static_cast<size_t>(apDelay), apGain);
                    } else {
                        s = string_.readHermite(d);
                    }
                } else {
                    s = string_.readHermite(d);
                }

                if constexpr (!isDispersion) {
                    const float value = std::abs(s) - 0.025f;
                    const float sign = s > 0.0f ? 1.0f : -1.5f;
                    curvedBridge = (std::abs(value) + value) * sign;
                }

                s += *in;
                s = std::clamp(s, -20.0f, 20.0f);

                s = dcBlocker.process(s);
                s = dampingFilter.process(s, Svf::Mode::LowPass);
                string_.write(s);

                outSample[1] = outSample[0];
                outSample[0] = s;
            }
            out[n] += crossfade(outSample[1], outSample[0], srcPhase);
            ++in;
        }
        delay_ = delayTarget;
    }

    static float interpolate(const float* table, float index, float size) {
        index *= size;
        const auto indexIntegral = static_cast<int>(index);
        const float indexFractional = index - static_cast<float>(indexIntegral);
        const float a = table[indexIntegral];
        const float b = table[indexIntegral + 1];
        return a + (b - a) * indexFractional;
    }

    double sampleRateHz = 48000.0;
    DelayLine<float, kDelayLineSize> string_;
    DelayLine<float, kDelayLineSize / 4> stretch_;
    Svf dampingFilter;
    DcBlocker dcBlocker;
    float delay_ = 100.0f;
    float dispersionNoise = 0.0f;
    float curvedBridge = 0.0f;
    float srcPhase = 0.0f;
    float outSample[2] = {0.0f, 0.0f};
    std::uint32_t rngState = 0x2545f491u;
};

} // namespace ruidocore
