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

namespace ruidocore {

// One-pole decay envelope. Rate-scaled coefficients preserve decay time.
class DecayEnvelope {
public:
    void reset() { value_ = 0.0f; }
    void trigger() { value_ = 1.0f; }
    // `decay`: per-update decay coefficient (0..1) - same convention and
    // same rescaling discipline as LpgEnvelope's own process*() methods
    // (see FmEngine.h's own comment on why a reference block-rate constant
    // needs dividing by kBlockSize before use in a per-sample update).
    void process(float decay) { value_ *= (1.0f - decay); }
    float value() const { return value_; }

private:
    float value_ = 0.0f;
};

} //
