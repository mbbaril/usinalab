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

#include <cmath>
#include <cstddef>

namespace ruidocore {

// A faithful port of Mutable Instruments' stmlib `Limiter`
// (stmlib/dsp/limiter.h, MIT licensed, github.com/pichenettes/stmlib,
// downloaded and read directly). This is the per-voice output safety
// stage real Plaits' `ChannelPostProcessor::Process()` runs every
// engine's raw output through before it ever reaches the mix - a fast-
// attack (0.05/sample), very-slow-release (0.00002/sample) peak
// follower that scales the signal down once its running peak exceeds
// unity, with 0.8 headroom on top. RuidoCore's engines never had an
// equivalent stage (each engine was ported for its own synthesis
// formulas, not this final safety net), which is what let Modal's
// high-Q resonances and String's voice pile-up blow past 2x-4x
// headroom under real sequenced/multi-voice playback (2026-08-27,
// "quase me fez passar mal" - a real listener-safety bug, not a
// tonal-quality one). Applied once per voice per output path (main
// and aux each get their own instance, matching real Plaits).
class Limiter {
public:
    void init() {
        peak = 0.5f;
    }

    void process(float* inOut, size_t size) {
        while (size--) {
            const float s = *inOut;
            const float error = std::fabs(s) - peak;
            peak += (error > 0.0f ? 0.05f : 0.00002f) * error;
            const float gain = (peak <= 1.0f) ? 1.0f : 1.0f / peak;
            *inOut++ = s * gain * 0.8f;
        }
    }

private:
    float peak = 0.5f;
};

} // namespace ruidocore
