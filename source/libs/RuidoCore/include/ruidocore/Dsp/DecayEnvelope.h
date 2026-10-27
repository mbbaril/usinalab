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

// A trivial one-pole decay envelope - ported faithfully from Mutable
// Instruments' Plaits (plaits/dsp/envelope.h's `DecayEnvelope`, MIT
// licensed, github.com/pichenettes/eurorack). Distinct from
// `LpgEnvelope` (LowPassGate.h): Plaits' own Voice class runs BOTH off
// the same per-block decay-time formula but as two SEPARATE state
// instances - one drives the LPG's gain/brightness (an asymmetric
// open/close vactrol model), the other (this one) is normalled to the
// FM/TIMBRE/MORPH CV inputs whenever nothing else is patched into them,
// giving every Plaits voice a free built-in "hit" envelope for pitch/
// timbre/morph modulation without needing an external LFO/envelope
// patched in (2026-08-27, user: "o plaits possui um envelope interno
// que pode ser usado pra criar um pitch envelope também" - this is that
// exact mechanism, faithfully separated out as its own shared module the
// same way LowPassGate.h already is, so any future voice can reuse it
// for pitch/timbre/morph "punch" the same way real Plaits patches do).
class DecayEnvelope {
public:
    void reset() { value_ = 0.0f; }
    void trigger() { value_ = 1.0f; }
    // `decay`: per-update decay coefficient (0..1) - same convention and
    // same rescaling discipline as LpgEnvelope's own process*() methods
    // (see FmEngine.h's own comment on why a Plaits block-rate constant
    // needs dividing by kBlockSize before use in a per-sample update).
    void process(float decay) { value_ *= (1.0f - decay); }
    float value() const { return value_; }

private:
    float value_ = 0.0f;
};

} // namespace ruidocore
