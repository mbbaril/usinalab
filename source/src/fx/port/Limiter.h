// ---------------------------------------------------------------------------
// This file ports code from Emilie Gillet's Eurorack firmware (https://github.com/pichenettes/eurorack), used under
// the MIT licence. The upstream file it derives from: beads/dsp/limiter.h ("Mono and stereo limiter."), read at
// eurorack f4cfa76 (D:/upstream/eurorack), ported line for line.
//
//   Copyright 2014 Emilie Gillet.
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//   THE SOFTWARE.
//
// The Usina as a whole, including this port, is licensed under the GPL-3.0 - see LICENSE at the repository root.
// See also THIRD_PARTY.md.
// ---------------------------------------------------------------------------
// What changed from upstream, and only this: the namespace (usina::port); the two attack coefficients upstream writes
// inline (0.1, 0.5 per sample) held in members with upstream's values, and set_rate_ratio(k) to keep their time
// constants when the limiter runs at k times the rate it was written for (1 - (1 - c)^(1/k)); untouched (k = 1), it
// is upstream's limiter, operation for operation.
#pragma once
#include "Reverb.h"
#include "StmlibDsp.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace usina::port {

class Limiter {
public:
    Limiter() {}
    ~Limiter() {}
    Limiter(const Limiter&) = delete;
    void operator=(const Limiter&) = delete;

    void Init(float decay, float max_peak)
    {
        peak_ = 0.5f;
        peak_lp_ = 0.5f;
        decay_ = decay;
        max_peak_ = max_peak;
    }

    void set_rate_ratio(float k)
    {
        attack_ = k == 1.0f ? 0.1f : 1.0f - std::pow(1.0f - 0.1f, 1.0f / k);
        attack_lp_ = k == 1.0f ? 0.5f : 1.0f - std::pow(1.0f - 0.5f, 1.0f / k);
    }

    void Process(FloatFrame* in_out, size_t size)
    {
        while (size--) {
            const float peak = std::min(std::max(fabsf(in_out->l), fabsf(in_out->r)), 40.0f);
            slope(peak_, peak, attack_, decay_);
            slope(peak_lp_, peak_, attack_lp_, decay_);
            float gain = (peak_lp_ <= max_peak_ ? 1.0f : max_peak_ / peak_lp_);
            in_out->l *= gain;
            in_out->r *= gain;
            ++in_out;
        }
    }

private:
    float peak_ = 0.5f;
    float peak_lp_ = 0.5f;
    float decay_ = 0.0f;
    float max_peak_ = 0.0f;
    float attack_ = 0.1f;
    float attack_lp_ = 0.5f;
};

} // namespace usina::port
