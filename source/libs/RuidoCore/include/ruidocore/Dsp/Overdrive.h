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

#include "SoftClip.h"

namespace ruidocore {

// Overdrive DSP implementation.
// See THIRD_PARTY.md and the preserved source-license notice for attribution.
struct Overdrive {
    float preGain = 0.0f;
    float postGain = 1.0f;

    void setDrive(float drive) {
        const float drive2 = drive * drive;
        const float preGainA = drive * 0.5f;
        const float preGainB = drive2 * drive2 * drive * 24.0f;
        preGain = preGainA + (preGainB - preGainA) * drive2;
        const float driveSquashed = drive * (2.0f - drive);
        postGain = 1.0f / softClip(0.33f + driveSquashed * (preGain - 0.33f));
    }

    float process(float in) const { return softClip(preGain * in) * postGain; }
};

} //
