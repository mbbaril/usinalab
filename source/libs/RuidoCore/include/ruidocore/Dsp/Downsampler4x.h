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

namespace ruidocore {

// 4x decimation via a short (4-tap, effectively 8-tap once the
// overlapping head/tail windows are accounted for) linear-phase FIR
// lowpass - faithfully following Mutable Instruments' own `Downsampler`
// (plaits/dsp/downsampler/4x_downsampler.h, MIT-licensed,
// github.com/pichenettes/eurorack, read for this library's own synth-
// engine research, see docs/SYNTH_ENGINE_RESEARCH.md), including its
// exact FIR coefficients (`lut_4x_downsampler_fir`, plaits/resources.cc)
// - the user asked explicitly to stay as faithful as possible to the
// Mutable modules rather than substitute a different filter design
// (2026-08-27, "prefiro fazer tudo o mais fiel possivel aos modulos da
// mutable" / "gosto do fir").
//
// Why the head/tail split: a 4-tap FIR applied at 4x the output rate
// spans energy across TWO output periods, not one - each oversampled
// input sample contributes partly to the output sample about to be
// read, partly to the NEXT one. Accumulating into two running sums and
// rotating them (`Read()`) is the standard way to implement that
// without a full ring-buffer/convolution loop.
class Downsampler4x {
public:
    static constexpr int kFactor = 4;

    void reset() { head = tail = 0.0f; }

    // Call once per oversampled sample, with `i` cycling 0..3 across
    // every 4 calls (i.e. i = sampleIndexWithinOutputPeriod).
    void accumulate(int i, float sample) {
        head += sample * kFir[3 - (i & 3)];
        tail += sample * kFir[i & 3];
    }

    // Call once every 4 accumulate() calls to get the next decimated
    // output sample.
    float read() {
        const float value = head;
        head = tail;
        tail = 0.0f;
        return value;
    }

private:
    static constexpr float kFir[4] = {0.02442415f, 0.09297315f, 0.16712938f, 0.21547332f};
    float head = 0.0f;
    float tail = 0.0f;
};

} // namespace ruidocore
