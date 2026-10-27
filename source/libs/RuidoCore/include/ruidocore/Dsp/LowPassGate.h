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

#include "Svf.h"

#include <algorithm>

namespace ruidocore {

// A percussive envelope modeled after a real analog low-pass gate's
// vactrol (an LED+photoresistor opto-coupler - the actual component real
// LPG circuits like the Buchla 292/Serge resonant EQ use): gain AND
// filter cutoff frequency rise and fall TOGETHER from one nonlinear
// state variable, not independently - a struck/plucked sound that gets
// duller as it gets quieter, the way a real vactrol-driven LPG (and real
// physical objects) actually behave, not just a volume envelope with a
// separately-configured filter.
//
// Ported faithfully from Mutable Instruments' Plaits
// (plaits/dsp/envelope.h's `LPGEnvelope`, MIT licensed,
// github.com/pichenettes/eurorack, read for this library's own synth-
// engine research, see docs/SYNTH_ENGINE_RESEARCH.md) - kept as a
// shared module here (not duplicated per engine) specifically so any
// future percussive voice (not just FM) can reuse the same real
// mechanism, per the user's own explicit request (2026-08-27, "esse lpg
// pode ser inclusive ser portado pra outras vozes").
class LpgEnvelope {
public:
    void reset() {
        vactrolState = 0.0f;
        gainValue = 1.0f;
        frequencyValue = 0.5f;
        hfBleedValue = 0.0f;
        rampingUp = false;
    }

    void trigger() { rampingUp = true; }

    // `attack`: how fast the vactrol opens on trigger (per-sample
    // increment, NOT a time in seconds - higher = faster).
    // `shortDecay`/`decayTail`: together shape the decay curve - see
    // process()'s own comment on the nonlinear blend between them.
    // `hf`: 0..1, how much high-frequency content "bleeds" straight
    // through the gate even when mostly closed (a fully-closed LPG on
    // real hardware is never a perfectly silent brick-wall cut).
    void processPing(float attack, float shortDecay, float decayTail, float hf) {
        if (rampingUp) {
            vactrolState += attack;
            if (vactrolState >= 1.0f) {
                vactrolState = 1.0f;
                rampingUp = false;
            }
        }
        processLevel(rampingUp ? vactrolState : 0.0f, shortDecay, decayTail, hf);
    }

    float gain() const { return gainValue; }
    bool isRampingUp() const { return rampingUp; }
    // 0..0.5, already in this library's own Svf::setFrequencyAndResonance
    // convention (fraction of sample rate) - NOT a raw Hz value.
    float frequency() const { return frequencyValue; }
    float hfBleed() const { return hfBleedValue; }

private:
    void processLevel(float level, float shortDecay, float decayTail, float hf) {
        const float vactrolInput = level;
        const float vactrolError = vactrolInput - vactrolState;
        const float vactrolState2 = vactrolState * vactrolState;
        const float vactrolState4 = vactrolState2 * vactrolState2;
        const float tail = 1.0f - vactrolState;
        const float tail2 = tail * tail;
        // Opening (error > 0) always uses a fast fixed coefficient - a
        // vactrol's LED lights up quickly. Closing blends `shortDecay`
        // (dominant early, when the state is still near 1 - the "short"
        // part of a two-stage decay) with `decayTail` (dominant once the
        // state has dropped, via the (1 - state^4) weighting - the long
        // tail). This asymmetric open/close + two-stage-decay shape is
        // what makes it sound like a real component, not a plain one-
        // pole envelope.
        const float vactrolCoefficient =
            (vactrolError > 0.0f) ? 0.6f : shortDecay + (1.0f - vactrolState4) * decayTail;
        vactrolState += vactrolCoefficient * vactrolError;

        gainValue = vactrolState;
        frequencyValue = 0.003f + 0.3f * vactrolState4 + hf * 0.04f;
        hfBleedValue = (tail2 + (1.0f - tail2) * hf) * hf * hf;
    }

    float vactrolState = 0.0f;
    float gainValue = 1.0f;
    float frequencyValue = 0.5f;
    float hfBleedValue = 0.0f;
    bool rampingUp = false;
};

// Applies an LpgEnvelope's gain/frequency/hfBleed to a signal: scale by
// gain, lowpass at frequency, then bleed a bit of the PRE-filter signal
// back in proportional to hfBleed (a real LPG never fully removes highs
// even when mostly closed). Ported faithfully from Plaits'
// `plaits::LowPassGate`, adapted to use this library's own `Svf` instead
// of stmlib's (functionally equivalent ZDF/TPT topology, already built
// here - see Svf.h).
class LowPassGate {
public:
    void reset() { filter.reset(); }

    float process(float in, float gain, float frequency, float hfBleed) {
        // 0.4f matches Plaits' own fixed `set_f_q<FREQUENCY_DIRTY>(frequency, 0.4f)` call exactly (same resonance-parameter convention this library's own Svf already uses: resonance, not raw Q).
        filter.setFrequencyAndResonance(std::clamp(frequency, 0.0001f, 0.49f), 0.4f);
        const float s = in * gain;
        const float lp = filter.process(s, Svf::Mode::LowPass);
        return lp + (s - lp) * hfBleed;
    }

private:
    Svf filter;
};

} // namespace ruidocore
