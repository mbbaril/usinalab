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

// Exact port of stmlib::SoftClip/SoftLimit (stmlib/dsp/dsp.h, MIT
// licensed, github.com/pichenettes/stmlib, downloaded and read
// directly) - a cubic soft-clip curve, linear near zero, saturating
// smoothly toward +-1 as x approaches +-3, hard-clamped beyond that.
// Used here as a final master-bus safety net (2026-08-27) on top of
// the per-voice ruidocore::Limiter: real Plaits only ever runs ONE
// voice per module, so its per-voice limiter is also its final output
// stage. RuidoCore's engines are polyphonic (multiple round-robin
// voices summed per part, multiple parts summed per bus), so several
// individually-limited voices can still sum above unity - this stage
// is the part of the safety chain real Plaits never needed but this
// project does.
inline float softClip(float x) {
    if (x < -3.0f)
        return -1.0f;
    if (x > 3.0f)
        return 1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

} // namespace ruidocore
