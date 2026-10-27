// SPDX-License-Identifier: GPL-3.0-only
// Usina FM engine - the MACRO operator mode (22/10; the user: "que tal já adicionar o plaits aos engines, como
// macro"): an operator becomes the macro-oscillator module's 24 models, ported whole and bit for bit
// (src/engines/macro/plaits, tools/port/port_plaits.py, check_plaits.sh). This is the operator's side - what the
// module's voice (plaits/dsp/voice.cc) does around a model, without the module's own LPG and envelope (the operator's
// envelope and the voice's LPG stage are Usina's):
//   A  MODEL (the module's order)      B  HARMONICS      C  TIMBRE (0..200: 0..1)      D  MORPH
//   the note: the operator's pitch; the accent: the velocity; the trigger: the note (a strike at its start, held
//   while the key is) - the drums, the plucked and struck models need it, the others ignore it
//   the matrix's FM into it moves its pitch once a block, as the module's own FM input does (its blocks: 12 samples
//   - a slow modulator bends it, an audio-rate one averages out); 1.0 in (a modulator at full) = an octave
//   its output level: the module's own per model (its post-processing gains), its limiter where the module has one
// The models run at the module's own rate - 47 872.34 Hz, its codec's (its tuning, a0, is set for that rate: run at
// 48 kHz it would sound 4.6 cents sharp) - and are brought to the host's rate by the converter (src/dsp/Resampler.h,
// out and aux as its two channels), then up to the voice's rate by the engine's half-band stages. The latency this
// adds (about 0.7 ms) is the operator's own: a MACRO operator sounds that much after the voice's FM operators.
// The six-operator models play patch banks: the module's own (its author's favourite DX7 patches - not the author's
// to license) only in a personal build (CMake USINA_PERSONAL_FM_BANKS); else a bank of the instrument's INIT VOICE
// (built here from the format: a sine), until the user's own SysEx banks come (docs/BACKLOG.md).
// No allocation after prepare(). Each operator its own set of models (about 27 KB: their 16 KB of shared memory as the
// module's).
#pragma once
#include "FmMacroModels.h"
#include "FmVoiceFilter.h"
#include "dsp/Resampler.h"
#include "engines/macro/plaits/dsp/engine/additive_engine.h"
#include "engines/macro/plaits/dsp/engine/bass_drum_engine.h"
#include "engines/macro/plaits/dsp/engine/chord_engine.h"
#include "engines/macro/plaits/dsp/engine/fm_engine.h"
#include "engines/macro/plaits/dsp/engine/grain_engine.h"
#include "engines/macro/plaits/dsp/engine/hi_hat_engine.h"
#include "engines/macro/plaits/dsp/engine/modal_engine.h"
#include "engines/macro/plaits/dsp/engine/noise_engine.h"
#include "engines/macro/plaits/dsp/engine/particle_engine.h"
#include "engines/macro/plaits/dsp/engine/snare_drum_engine.h"
#include "engines/macro/plaits/dsp/engine/speech_engine.h"
#include "engines/macro/plaits/dsp/engine/string_engine.h"
#include "engines/macro/plaits/dsp/engine/swarm_engine.h"
#include "engines/macro/plaits/dsp/engine/virtual_analog_engine.h"
#include "engines/macro/plaits/dsp/engine/waveshaping_engine.h"
#include "engines/macro/plaits/dsp/engine/wavetable_engine.h"
#include "engines/macro/plaits/dsp/engine2/chiptune_engine.h"
#include "engines/macro/plaits/dsp/engine2/phase_distortion_engine.h"
#include "engines/macro/plaits/dsp/engine2/six_op_engine.h"
#include "engines/macro/plaits/dsp/engine2/string_machine_engine.h"
#include "engines/macro/plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "engines/macro/plaits/dsp/engine2/wave_terrain_engine.h"
#include "port/stmlib/dsp/limiter.h"
#include "port/stmlib/utils/buffer_allocator.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace usina::fm {

// the six-operator models' banks: three (the module's three models), each 32 patches of 128 bytes
const uint8_t* macroFmBank(int bank);                    // (FmMacro.cpp: the personal build's, or INIT VOICE)

class Macro {
public:
    static constexpr double kModuleRate = 47872.34;      // (plaits/dsp/dsp.h kCorrectedSampleRate)
    static constexpr int kBlock = 12;                    // (plaits/dsp/dsp.h kBlockSize)

    // hostRate: the plugin's; oversample: the voice's rate / the host's (1, 2, 4, 8, 16)
    void prepare(double hostRate, int oversample)
    {
        up = 0;
        while ((1 << up) < oversample && up < 4) ++up;
        conv.prepare(kModuleRate, hostRate, kBlock);
        engines = { &vaVcf, &phaseDistortion, &sixOp, &sixOp, &sixOp, &waveTerrain, &stringMachine, &chiptune,
                    &virtualAnalog, &waveshaping, &fm, &grain, &additive, &wavetable, &chord, &speech, &swarm,
                    &noise, &particle, &string, &modal, &bassDrum, &snareDrum, &hiHat };
        // each model's output stage, as the module's voice registers them (plaits/dsp/voice.cc Init: already
        // enveloped, out gain, aux gain; a negative gain: through a limiter) - without it the gains stay at the zeroed
        // memory's 0 (measured 22/10: every model silent)
        static const usina_plaits::PostProcessingSettings kPost[kMacroModels] = {
            { 1.0f, 1.0f, false }, { 0.7f, 0.7f, false }, { 1.0f, 1.0f, true }, { 1.0f, 1.0f, true },
            { 1.0f, 1.0f, true }, { 0.7f, 0.7f, false }, { 0.8f, 0.8f, false }, { 0.5f, 0.5f, false },
            { 0.8f, 0.8f, false }, { 0.7f, 0.6f, false }, { 0.6f, 0.6f, false }, { 0.7f, 0.6f, false },
            { 0.8f, 0.8f, false }, { 0.6f, 0.6f, false }, { 0.8f, 0.8f, false }, { -0.7f, 0.8f, false },
            { -3.0f, 1.0f, false }, { -1.0f, -1.0f, false }, { -2.0f, 1.0f, false }, { -1.0f, 0.8f, true },
            { -1.0f, 0.8f, true }, { 0.8f, 0.8f, true }, { 0.8f, 0.8f, true }, { 0.8f, 0.8f, true } };
        for (int i = 0; i < kMacroModels; ++i) post[(size_t)i] = kPost[i];
        for (auto* e : engines) {                        // (as the module: every model on the same memory)
            allocator.Init(shared, sizeof shared);
            e->Init(&allocator);
        }
        model = -1;
        reset();
    }
    void reset()
    {
        conv.reset();
        limiter.Init();
        for (auto& s : ups) s.reset();
        pos = queued = 0;
        fmSum = 0.0;
        fmCount = 0;
        rising = false;
    }
    // a note: its strike (the trigger's rising edge at the next block)
    void strike() { rising = true; }

    // one sample at the voice's rate: the model and its values (B..D 0..1), the note (MIDI, fractional), the accent,
    // the key held, the matrix's input
    float process(int m, float harmonics, float timbre, float morph, float note, float accent, bool gate, float in)
    {
        fmSum += in;
        ++fmCount;
        if (pos >= queued) refill(m, harmonics, timbre, morph, note, accent, gate);
        return buf[(size_t)pos++];
    }

private:
    // the next host-rate sample (rendering a block of the model when the converter needs one), raised to the voice's
    // rate
    void refill(int m, float harmonics, float timbre, float morph, float note, float accent, bool gate)
    {
        while (conv.available() < 1) render(m, harmonics, timbre, morph, note, accent, gate);
        float l, r;
        conv.pull(&l, &r, 1);
        double x[16];
        x[0] = l;
        int n = 1;
        for (int s = 0; s < up; ++s) {                   // (each stage doubles: 1 -> 2 -> 4 ...)
            double y[16];
            for (int i = 0; i < n; ++i) ups[(size_t)s].up(x[i], y[2 * i], y[2 * i + 1]);
            n *= 2;
            for (int i = 0; i < n; ++i) x[i] = y[i];
        }
        for (int i = 0; i < n; ++i) buf[(size_t)i] = (float)x[i];
        queued = n;
        pos = 0;
    }

    void render(int m, float harmonics, float timbre, float morph, float note, float accent, bool gate)
    {
        m = ((m % kMacroModels) + kMacroModels) % kMacroModels;
        auto* e = engines[(size_t)m];
        if (m != model) {                                // (as the module's voice: the user data, a reset)
            e->LoadUserData(m >= 2 && m <= 4 ? macroFmBank(m - 2) : nullptr);
            e->Reset();
            limiter.Init();
            model = m;
        }
        const float fm = fmCount > 0 ? (float)(fmSum / fmCount) : 0.0f;
        fmSum = 0.0;
        fmCount = 0;
        usina_plaits::EngineParameters p;
        p.note = note + 12.0f * fm;
        p.harmonics = harmonics;
        p.timbre = timbre;
        p.morph = morph;
        p.accent = accent;
        p.trigger = rising ? (usina_plaits::TRIGGER_RISING_EDGE | usina_plaits::TRIGGER_HIGH)
                           : (gate ? usina_plaits::TRIGGER_HIGH : usina_plaits::TRIGGER_LOW);
        rising = false;
        bool enveloped = false;
        float out[kBlock], aux[kBlock];
        e->Render(p, out, aux, kBlock, &enveloped);
        // the module's output stage: its gain per model, a limiter where the gain is negative (plaits/dsp/voice.h
        // ChannelPostProcessor; its 16 bits and its LPG left out)
        const float g = post[(size_t)m].out_gain, ga = post[(size_t)m].aux_gain;   // (by model: the six-op ones share
                                                                                    // one engine)
        if (g < 0.0f) limiter.Process(-g, out, kBlock);
        const float og = g < 0.0f ? 1.0f : g, oa = ga < 0.0f ? 1.0f : ga;
        for (int i = 0; i < kBlock; ++i) {
            out[i] *= og;
            aux[i] *= oa;
        }
        conv.push(out, aux, kBlock);
    }

    usina_plaits::VirtualAnalogVCFEngine vaVcf;
    usina_plaits::PhaseDistortionEngine phaseDistortion;
    usina_plaits::SixOpEngine sixOp;
    usina_plaits::WaveTerrainEngine waveTerrain;
    usina_plaits::StringMachineEngine stringMachine;
    usina_plaits::ChiptuneEngine chiptune;
    usina_plaits::VirtualAnalogEngine virtualAnalog;
    usina_plaits::WaveshapingEngine waveshaping;
    usina_plaits::FMEngine fm;
    usina_plaits::GrainEngine grain;
    usina_plaits::AdditiveEngine additive;
    usina_plaits::WavetableEngine wavetable;
    usina_plaits::ChordEngine chord;
    usina_plaits::SpeechEngine speech;
    usina_plaits::SwarmEngine swarm;
    usina_plaits::NoiseEngine noise;
    usina_plaits::ParticleEngine particle;
    usina_plaits::StringEngine string;
    usina_plaits::ModalEngine modal;
    usina_plaits::BassDrumEngine bassDrum;
    usina_plaits::SnareDrumEngine snareDrum;
    usina_plaits::HiHatEngine hiHat;
    std::array<usina_plaits::Engine*, kMacroModels> engines {};
    std::array<usina_plaits::PostProcessingSettings, kMacroModels> post {};
    alignas(16) char shared[16384] {};
    usina_stmlib::BufferAllocator allocator;
    usina_stmlib::Limiter limiter;
    usina::Resampler conv;
    std::array<HalfBand, 4> ups {};
    std::array<float, 16> buf {};
    int up = 0, pos = 0, queued = 0, model = -1, fmCount = 0;
    double fmSum = 0.0;
    bool rising = false;
};

Macro* newMacro();                                       // (in zeroed memory: FmMacro.cpp)

} // namespace usina::fm
