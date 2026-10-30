// SPDX-License-Identifier: GPL-3.0-only
// Usina voice low-pass gate: coupled filter and gain with a slow photoresistive
// response, after the voice filter. Attribution and source files: THIRD_PARTY.md.
// PING rises at a pitch-dependent rate, then decays; EG2 follows the compressed
// filter envelope; SOURCE follows a modulation source. DECAY and COLOR: 0..1.
// Control blocks span round(12 * fs / 48000) samples; gain ramps within each.
// At 48 kHz the reference coefficient is unchanged. At other rates, holding
// g * fs preserves the response below cutoff despite bilinear frequency warping.
// No allocation during rendering.
#pragma once
#include "engines/macro/models/dsp/dsp.h"
#include "engines/macro/models/dsp/engine/engine.h"
#include "engines/macro/models/dsp/envelope.h"
#include "engines/macro/models/dsp/fx/low_pass_gate.h"
#include "port/dsputil/dsp/units.h"
#include <algorithm>
#include <cmath>

namespace usina::fm {

class VoiceLpg {
public:
    enum Mode { kOff = 0, kPing, kEg2, kSource };          // (SOURCE: as EG2, its level a modulation source)

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
        value += increment;                              // (the utilities' ParameterInterpolator, a sample at a time)
        if (pos == 0) prevGain = value;
        const float s = x * value;
        const float lp = filter.Process<usina_dsputil::FILTER_MODE_LOW_PASS>(s);
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
        using usina_models::kBlockSize;
        using usina_models::kSampleRate;
        const float shortDecay = (200.0f * kBlockSize) / kSampleRate * usina_dsputil::SemitonesToRatio(-96.0f * decay);
        const float decayTail = (20.0f * kBlockSize) / kSampleRate *
                                    usina_dsputil::SemitonesToRatio(-72.0f * decay + 12.0f * hf) - shortDecay;
        if (mode == kEg2 || mode == kSource) {
            const float compressed = 1.3f * eg2 / (0.3f + std::fabs(eg2));
            env.ProcessLP(compressed, shortDecay, decayTail, hf);
        } else {
            const float attack = usina_models::NoteToFrequency(note) * float(kBlockSize) * 2.0f;
            env.ProcessPing(attack, shortDecay, decayTail, hf);
        }
        // LowPassGate::Process's own start: the gain ramped from the last block's over this one, the filter set
        value = prevGain;
        increment = (env.gain() - prevGain) / static_cast<float>(block);
        if (native) {
            filter.set_f_q<usina_dsputil::FREQUENCY_DIRTY>(env.frequency(), 0.4f);
        } else {                                         // (the module's response below its cutoff: g fs held)
            const float g48 = usina_dsputil::OnePole::tan<usina_dsputil::FREQUENCY_DIRTY>(env.frequency());
            filter.set_g_q(g48 * warp, 0.4f);
        }
        bleed = env.hf_bleed();
        pending = false;
    }

    usina_models::LPGEnvelope env;
    usina_dsputil::Svf filter;
    double fs = 48000.0;
    int block = 12, pos = 0;
    bool native = true, pending = false;
    float warp = 1.0f, prevGain = 0.0f, value = 0.0f, increment = 0.0f, bleed = 0.0f;
};

} // namespace usina::fm
