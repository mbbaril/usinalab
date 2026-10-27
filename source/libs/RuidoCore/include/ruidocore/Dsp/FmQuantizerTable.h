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

// 130-entry lookup table mapping a 0..1 "harmonics" position to a
// semitone offset for the FM modulator ratio, ported verbatim from
// Mutable Instruments' Plaits (plaits/resources.cc,
// lut_fm_frequency_quantizer - MIT licensed,
// github.com/pichenettes/eurorack, read for this library's own synth-
// engine research, see docs/SYNTH_ENGINE_RESEARCH.md). Copied directly
// rather than independently reimplemented, per the user's own stated
// preference for fidelity to the Mutable modules.
//
// Snapping the modulator ratio to musically-curated semitone steps
// (rather than a raw continuous multiplier) is what makes FM harmonics
// land on recognizable harmonic/inharmonic relationships instead of
// sounding like arbitrary detuning - see FmQuantizerTable.cpp's own
// header for the interpolation convention this table is read with
// (stmlib::Interpolate: index = harmonics01 * 128, linear-interpolated
// between the two nearest entries).
extern const float kFmFrequencyQuantizerTable[130];

// index01: 0..1. Returns a semitone OFFSET (added to the carrier's own
// note number to get the modulator's note number) - matches Plaits' own
// `Interpolate(lut_fm_frequency_quantizer, harmonics, 128.0f)` call
// exactly.
inline float fmFrequencyQuantizerSemitones(float index01) {
    float index = index01 * 128.0f;
    if (index < 0.0f)
        index = 0.0f;
    if (index > 128.0f)
        index = 128.0f;
    const int indexIntegral = static_cast<int>(index);
    const float indexFractional = index - static_cast<float>(indexIntegral);
    const float a = kFmFrequencyQuantizerTable[indexIntegral];
    const float b = kFmFrequencyQuantizerTable[indexIntegral + 1];
    return a + (b - a) * indexFractional;
}

} // namespace ruidocore
