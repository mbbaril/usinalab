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

#include "Svf.h"

#include <algorithm>

namespace ruidocore {

// Models a photoresistive low-pass gate: one nonlinear state couples gain
// and cutoff, reducing brightness as the amplitude decays.
//
// Low-pass gate envelope with pitch-dependent opening and slower closing response.
// See THIRD_PARTY.md and the preserved license notice for source attribution.
class LpgEnvelope {
public:
    void reset() {
        optoState = 0.0f;
        gainValue = 1.0f;
        frequencyValue = 0.5f;
        hfBleedValue = 0.0f;
        rampingUp = false;
    }

    void trigger() { rampingUp = true; }

    // `attack`: how fast the photoresistive cell opens on trigger (per-sample
    // increment, NOT a time in seconds - higher = faster).
    // `shortDecay`/`decayTail`: together shape the decay curve - see
    // process()'s own comment on the nonlinear blend between them.
    // `hf`: 0..1, how much high-frequency content "bleeds" straight
    // through the gate even when mostly closed (a fully-closed LPG on
    // real hardware is never a perfectly silent brick-wall cut).
    void processPing(float attack, float shortDecay, float decayTail, float hf) {
        if (rampingUp) {
            optoState += attack;
            if (optoState >= 1.0f) {
                optoState = 1.0f;
                rampingUp = false;
            }
        }
        processLevel(rampingUp ? optoState : 0.0f, shortDecay, decayTail, hf);
    }

    float gain() const { return gainValue; }
    bool isRampingUp() const { return rampingUp; }
    // 0..0.5, already in this library's own Svf::setFrequencyAndResonance
    // convention (fraction of sample rate) - NOT a raw Hz value.
    float frequency() const { return frequencyValue; }
    float hfBleed() const { return hfBleedValue; }

private:
    void processLevel(float level, float shortDecay, float decayTail, float hf) {
        const float optoInput = level;
        const float optoError = optoInput - optoState;
        const float optoState2 = optoState * optoState;
        const float optoState4 = optoState2 * optoState2;
        const float tail = 1.0f - optoState;
        const float tail2 = tail * tail;
        // Opening (error > 0) always uses a fast fixed coefficient - a
        // photoresistive cell's LED lights up quickly. Closing blends `shortDecay`
        // (dominant early, when the state is still near 1 - the "short"
        // part of a two-stage decay) with `decayTail` (dominant once the
        // state has dropped, via the (1 - state^4) weighting - the long
        // tail). This asymmetric open/close + two-stage-decay shape is
        // what makes it sound like a real component, not a plain one-
        // pole envelope.
        const float optoCoefficient =
            (optoError > 0.0f) ? 0.6f : shortDecay + (1.0f - optoState4) * decayTail;
        optoState += optoCoefficient * optoError;

        gainValue = optoState;
        frequencyValue = 0.003f + 0.3f * optoState4 + hf * 0.04f;
        hfBleedValue = (tail2 + (1.0f - tail2) * hf) * hf * hf;
    }

    float optoState = 0.0f;
    float gainValue = 1.0f;
    float frequencyValue = 0.5f;
    float hfBleedValue = 0.0f;
    bool rampingUp = false;
};

// Applies an LpgEnvelope's gain/frequency/hfBleed to a signal: scale by
// gain, lowpass at frequency, then bleed a bit of the PRE-filter signal
// back in proportional to hfBleed (a real LPG never fully removes highs
// even when mostly closed). Ported from reference
// `LowPassGate`, adapted to use this library's own `Svf` instead
// of the reference's (functionally equivalent ZDF/TPT topology, already built
// here - see Svf.h).
class LowPassGate {
public:
    void reset() { filter.reset(); }

    float process(float in, float gain, float frequency, float hfBleed) {
        // 0.4f matches reference fixed `set_f_q<FREQUENCY_DIRTY>(frequency, 0.4f)` call exactly (same resonance-parameter convention this library's own Svf already uses: resonance, not raw Q).
        filter.setFrequencyAndResonance(std::clamp(frequency, 0.0001f, 0.49f), 0.4f);
        const float s = in * gain;
        const float lp = filter.process(s, Svf::Mode::LowPass);
        return lp + (s - lp) * hfBleed;
    }

private:
    Svf filter;
};

} //
