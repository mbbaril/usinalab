// SPDX-License-Identifier: GPL-3.0-only
// Usina FM engine - the LPG, a voice's low pass gate (docs/BACKLOG.md; the user, 21/10: "precisamos de um lpg também"):
// the filter and the VCA together with a vactrol's response, after the voice filter. The gate and its envelope are the
// Plaits module's, from the port of the whole module (src/engines/macro/plaits: dsp/fx/low_pass_gate.h, the
// LPGEnvelope of dsp/envelope.h, NoteToFrequency of dsp/engine/engine.h); this is the voice's side, with the module's laws
// (plaits/dsp/voice.cc):
//   PING  each note strikes it (the "plonk"): the vactrol rises at a speed set by the note, then falls
//   EG2   it follows EG2, the voice's filter envelope (the module's LEVEL input, softly compressed as it does)
//   DECAY 0..1 its fall (short_decay, decay_tail), COLOR 0..1 the high end that leaks past it (hf)
// The module works in blocks of 12 samples at 48 kHz: here, a block of round(12 fs / 48 kHz) samples at the voice's
// rate - its envelope once a block, its gain ramped across the block - so the times hold at any rate (48, 96, 192 kHz
// exactly; the 44.1 kHz family within 0.23 %). The gate's filter: at 48 kHz the module's own coefficient (bit for bit,
// tools/port/check_lpg.sh); at any other rate the module's response below its cutoff, kept by holding g fs (g the
// filter's coefficient). Not the "same analog cutoff": wide open the module's cutoff sits near its Nyquist, where the
// bilinear map bends it - its sound is the bent one (measured 21/10: the analog cutoff kept, 5 kHz came out 0.6 dB
// darker at 96 kHz; g fs held, within 0.1 dB, tests/LpgTest CUTOFF).
// Plain C++, no allocation.
#pragma once
#include "engines/macro/plaits/dsp/dsp.h"
#include "engines/macro/plaits/dsp/engine/engine.h"
#include "engines/macro/plaits/dsp/envelope.h"
#include "engines/macro/plaits/dsp/fx/low_pass_gate.h"
#include "port/stmlib/dsp/units.h"
#include <algorithm>
#include <cmath>

namespace usina::fm {

class VoiceLpg {
public:
    enum Mode { kOff = 0, kPing, kEg2 };

    // (the module's classes cannot be copied; a voice is reset by assignment in Engine::prepare: a copy takes the
    // settings and starts from rest)
    VoiceLpg() { reset(); }
    VoiceLpg(const VoiceLpg& o) { copySettings(o); }
    VoiceLpg& operator=(const VoiceLpg& o)
    {
        if (this != &o) copySettings(o);
        return *this;
    }

    void prepare(double rate)
    {
        fs = rate;
        block = std::max(1, (int)std::lround(12.0 * fs / 48000.0));
        native = fs == 48000.0;
        warp = (float)(48000.0 / fs);
        reset();
    }
    void reset()
    {
        env.Init();
        filter.Init();
        prevGain = 0.0f;
        pos = 0;
        pending = false;
    }
    void trigger()
    {
        env.Trigger();
        pending = true;                                  // (struck: not closed until its block has run)
    }

    // one sample; mode, decay 0..1, color 0..1, eg2 0..1 (EG2's level), note (the voice's, for PING's rise)
    float process(float x, int mode, float decay, float color, float eg2, float note)
    {
        if (pos == 0) startBlock(mode, decay, color, eg2, note);
        if (++pos >= block) pos = 0;
        value += increment;                              // (stmlib's ParameterInterpolator, a sample at a time)
        if (pos == 0) prevGain = value;
        const float s = x * value;
        const float lp = filter.Process<usina_stmlib::FILTER_MODE_LOW_PASS>(s);
        return lp + (s - lp) * bleed;
    }
    // closed for good: PING's fall over, nothing left (the voice may end)
    bool closed() const { return !pending && env.gain() < 1.0e-5f && prevGain < 1.0e-5f; }

private:
    void copySettings(const VoiceLpg& o)
    {
        fs = o.fs;
        block = o.block;
        native = o.native;
        warp = o.warp;
        reset();
    }
    // the module's voice.cc, once a block: its laws for DECAY and COLOR, the envelope, the gate's settings
    void startBlock(int mode, float decay, float hf, float eg2, float note)
    {
        using usina_plaits::kBlockSize;
        using usina_plaits::kSampleRate;
        const float shortDecay = (200.0f * kBlockSize) / kSampleRate * usina_stmlib::SemitonesToRatio(-96.0f * decay);
        const float decayTail = (20.0f * kBlockSize) / kSampleRate *
                                    usina_stmlib::SemitonesToRatio(-72.0f * decay + 12.0f * hf) - shortDecay;
        if (mode == kEg2) {
            const float compressed = 1.3f * eg2 / (0.3f + std::fabs(eg2));
            env.ProcessLP(compressed, shortDecay, decayTail, hf);
        } else {
            const float attack = usina_plaits::NoteToFrequency(note) * float(kBlockSize) * 2.0f;
            env.ProcessPing(attack, shortDecay, decayTail, hf);
        }
        // LowPassGate::Process's own start: the gain ramped from the last block's over this one, the filter set
        value = prevGain;
        increment = (env.gain() - prevGain) / static_cast<float>(block);
        if (native) {
            filter.set_f_q<usina_stmlib::FREQUENCY_DIRTY>(env.frequency(), 0.4f);
        } else {                                         // (the module's response below its cutoff: g fs held)
            const float g48 = usina_stmlib::OnePole::tan<usina_stmlib::FREQUENCY_DIRTY>(env.frequency());
            filter.set_g_q(g48 * warp, 0.4f);
        }
        bleed = env.hf_bleed();
        pending = false;
    }

    usina_plaits::LPGEnvelope env;
    usina_stmlib::Svf filter;
    double fs = 48000.0;
    int block = 12, pos = 0;
    bool native = true, pending = false;
    float warp = 1.0f, prevGain = 0.0f, value = 0.0f, increment = 0.0f, bleed = 0.0f;
};

} // namespace usina::fm
