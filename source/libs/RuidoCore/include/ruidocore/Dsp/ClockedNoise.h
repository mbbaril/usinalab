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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ruidocore {

// PolyBLEP band-limited step correction - exact port of
// stmlib::ThisBlepSample/NextBlepSample (stmlib/dsp/polyblep.h, MIT
// licensed, github.com/pichenettes/stmlib, downloaded and read
// directly).
inline float thisBlepSample(float t) { return 0.5f * t * t; }
inline float nextBlepSample(float t) {
    t = 1.0f - t;
    return -0.5f * t * t;
}

// A faithful port of Mutable Instruments' Plaits `ClockedNoise`
// (plaits/dsp/noise/clocked_noise.h, MIT licensed,
// github.com/pichenettes/eurorack, downloaded and read directly) -
// white noise sample-and-held at a target frequency, PolyBLEP-corrected
// at each hold-boundary discontinuity, with a "raw" (un-held, pure
// white noise) component crossfaded in as the clock frequency climbs
// past 0.25 cycles/sample (where sample-and-hold noise starts sounding
// identical to raw noise anyway).
class ClockedNoise {
public:
    void init() {
        phase = 0.0f;
        sample = 0.0f;
        nextSample = 0.0f;
        frequency = 0.001f;
    }

    // Own PRNG (xorshift32) rather than matching Plaits' own generator
    // bit-for-bit - only the resulting statistical/spectral character
    // matters for noise, same reasoning as AnalogSnareDrum/AnalogHiHat's
    // own noise sources.
    float nextRandom01() {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return static_cast<float>(rngState) / 4294967295.0f;
    }

    void render(bool sync, float targetFrequency, float* out, size_t size) {
        targetFrequency = std::clamp(targetFrequency, 0.0f, 1.0f);
        frequency = targetFrequency; // no block-length parameter smoothing in this port - matches this library's usual per-render-call-constant convention

        if (sync)
            phase = 1.0f;

        while (size--) {
            float thisSample = nextSample;
            nextSample = 0.0f;

            const float rawSample = nextRandom01() * 2.0f - 1.0f;
            float rawAmount = 4.0f * (frequency - 0.25f);
            rawAmount = std::clamp(rawAmount, 0.0f, 1.0f);

            phase += frequency;
            if (phase >= 1.0f) {
                phase -= 1.0f;
                const float t = phase / frequency;
                const float newSample = rawSample;
                const float discontinuity = newSample - sample;
                thisSample += discontinuity * thisBlepSample(t);
                nextSample += discontinuity * nextBlepSample(t);
                sample = newSample;
            }
            nextSample += sample;
            *out++ = thisSample + rawAmount * (rawSample - thisSample);
        }
    }

private:
    float phase = 0.0f, sample = 0.0f, nextSample = 0.0f, frequency = 0.001f;
    std::uint32_t rngState = 0x2545f491u;
};

} // namespace ruidocore
