// SPDX-License-Identifier: GPL-3.0-only
// Shared texture reference scenarios: four media, granular/delay, SEED, freeze and mono.
// Process input gain, DSP and output clamping in blocks at a 4 kHz control rate.
// Build against reference integer input or adapted float input.
// Hash float bytes with 64-bit FNV-1a and compare complete output streams.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#ifdef TX_UPSTREAM
#define TX beads
#define STM stmlib
#else
#define TX usina_texture
#define STM usina_dsputil
#endif

namespace texture_scenarios {

constexpr int kScenarios = 12;
constexpr double kSeconds = 6.0;
constexpr size_t kMaxBlock = 24;                         // The codec's largest block: 48 kHz / 4000 = 12

struct Knobs {                                           // The panel, 0..1 unless noted
    float time, size, shape, pitch;                      // Pitch: semitones, as the detune pot law gives them
    float time_amt, size_amt, shape_amt, pitch_amt;      // Attenuverters, -1..1 (no CV: they set the randomness)
    float density, feedback, dry_wet, reverb;
    bool freeze, seed_button, seed_locked;
    uint8_t gain;                                        // 0: automatic gain
};

inline const char* scenarioName(int s)
{
    static const char* names[kScenarios] = {
        "digital granular", "digital delay", "vintage granular", "vintage delay", "clean-tape granular",
        "clean-tape delay", "dirty-tape granular", "dirty-tape delay", "digital granular seeds+freeze",
        "digital delay freeze", "clean-tape mono granular manual-gain", "dirty-tape delay manual-gain"
    };
    return names[s];
}

// The scenario's media (the processor's enum order: dirty tape, clean tape, vintage digital, digital) and mono flag
inline int scenarioMedia(int s)
{
    static const int m[kScenarios] = { 3, 3, 2, 2, 1, 1, 0, 0, 3, 3, 1, 0 };
    return m[s];
}
inline bool scenarioMono(int s) { return s == 10; }
inline bool scenarioDelay(int s) { return s == 1 || s == 3 || s == 5 || s == 7 || s == 9 || s == 11; }

// The knobs at time t (s): every control swept, at its own speed
inline Knobs knobsAt(int s, double t)
{
    Knobs k {};
    const double pi = 3.14159265358979323846;
    k.time = (float)(0.5 + 0.45 * std::sin(2 * pi * 0.13 * t));
    k.size = scenarioDelay(s) ? 1.0f : (float)(0.45 + 0.4 * std::sin(2 * pi * 0.21 * t + 1.0));
    k.shape = (float)(0.5 + 0.48 * std::sin(2 * pi * 0.17 * t + 2.0));
    k.pitch = (float)(std::fmod(t, 3.0) < 1.5 ? 0.0 : (s % 2 ? 7.0 : -12.0));
    k.time_amt = (float)(0.6 * std::sin(2 * pi * 0.11 * t));
    k.size_amt = (float)(std::fmod(t, 4.0) < 2.0 ? 0.3 : -0.4);
    k.shape_amt = (float)(0.2 * std::sin(2 * pi * 0.07 * t));
    k.pitch_amt = (float)(std::fmod(t, 5.0) < 2.5 ? 0.0 : 0.5);
    k.density = (float)(0.5 + 0.45 * std::sin(2 * pi * 0.09 * t + 0.5));
    k.feedback = (float)(0.35 + 0.3 * std::sin(2 * pi * 0.05 * t));
    k.dry_wet = (float)(0.6 + 0.35 * std::sin(2 * pi * 0.08 * t + 1.5));
    k.reverb = (float)(0.3 + 0.3 * std::sin(2 * pi * 0.06 * t + 0.7));
    k.freeze = (s == 8 || s == 9) && std::fmod(t, 2.5) > 1.6;
    k.seed_button = s == 8 && std::fmod(t, 0.37) < 0.05;
    k.seed_locked = s == 8 && t > 4.0;
    k.gain = (s == 10 || s == 11) ? 180 : 0;
    return k;
}

// The input, as the codec's 32-bit integers (the port gets them / 2^31: the same floats): a decaying noise burst, a
// chord, a bass, silence, a hot passage
inline int32_t inputAt(uint32_t& rng, double t, int channel)
{
    const double pi = 3.14159265358979323846;
    rng = rng * 1664525u + 1013904223u;
    const double noise = ((double)(int32_t)rng) / 2147483648.0;
    const double ph = std::fmod(t, 2.0);
    double x;
    if (ph < 0.6) x = noise * 0.5 * std::exp(-ph * 5.0);
    else if (ph < 1.3) x = 0.25 * (std::sin(2 * pi * 220.0 * t) + std::sin(2 * pi * (277.2 + channel) * t) +
                                   std::sin(2 * pi * 329.6 * t));
    else if (ph < 1.6) x = 0.6 * std::sin(2 * pi * 55.0 * t);
    else if (ph < 1.8) x = 0.0;
    else x = 0.95 * std::tanh(3.0 * std::sin(2 * pi * 110.0 * t)) + 0.04 * noise;
    if (x > 0.999) x = 0.999;
    if (x < -0.999) x = -0.999;
    return (int32_t)(x * 2147483648.0);
}

struct Fnv {
    uint64_t h = 1469598103934665603ull;
    void add(const void* p, size_t n)
    {
        const auto* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    }
};

// Runs one scenario; returns its hash, and its output (interleaved l, r) if out != nullptr
inline uint64_t run(int s, std::vector<float>* out)
{
    using namespace TX;
    struct Memory {
        uint8_t bank0[512 * 1024];                       // Processor memory: 512 KiB plus 256 KiB; split the first region into two banks.
        uint8_t bank2[256 * 1024];
        uint16_t reverb[32768];
    };
    // Reconstruct zero-initialized state per scenario. Init does not initialize
    // every input-gain field, so ordinary heap allocation is insufficient.
    static Memory* mem = new Memory();
    static void* processorMemory = std::malloc(sizeof(Processor));
    static void* gainMemory = std::malloc(sizeof(GainControl));
    std::memset(mem, 0, sizeof(Memory));
    std::memset(processorMemory, 0, sizeof(Processor));
    std::memset(gainMemory, 0, sizeof(GainControl));
    Processor* processor = new (processorMemory) Processor;
    GainControl* gain_control = new (gainMemory) GainControl;
    STM::Random::Seed(0x21);

    uint8_t* buffers[3] = { mem->bank0, mem->bank0 + 256 * 1024, mem->bank2 };
    processor->Init(buffers, mem->reverb);
    const Processor::Media media = Processor::Media(scenarioMedia(s));
    processor->Configure(media, scenarioMono(s));
    const float sr = processor->sample_rate();
    const size_t block = size_t(sr) / 4000;
    gain_control->Init(sr, media == Processor::MEDIA_DIRTY_TAPE
                               ? GainControl::SOFT_CLIPPING_MODE_ON
                               : (media == Processor::MEDIA_CLEAN_TAPE ? GainControl::SOFT_CLIPPING_MODE_DYNAMIC
                                                                         : GainControl::SOFT_CLIPPING_MODE_OFF),
                       true);

    Fnv fnv;
    uint32_t rng = 12345u + (uint32_t)s;
    const int blocks = int(kSeconds * sr) / int(block);
    STM::GateFlags previous_seed = STM::GATE_FLAG_LOW;
    for (int b = 0; b < blocks; ++b) {
        const double t = double(b) * double(block) / double(sr);
        const Knobs k = knobsAt(s, t);
#ifdef TX_UPSTREAM
        Codec::Frame in[kMaxBlock];
#else
        FloatFrame in[kMaxBlock];
#endif
        FloatFrame float_in[kMaxBlock], float_out[kMaxBlock];
        bool freeze[kMaxBlock];
        STM::GateFlags seed[kMaxBlock];
        for (size_t i = 0; i < block; ++i) {
            const double ti = t + double(i) / double(sr);
            const int32_t l = inputAt(rng, ti, 0), r = inputAt(rng, ti, 1);
#ifdef TX_UPSTREAM
            in[i].l = l;
            in[i].r = r;
#else
            in[i].l = static_cast<float>(l) / 2147483648.0f;
            in[i].r = static_cast<float>(r) / 2147483648.0f;
#endif
            freeze[i] = k.freeze;
            const bool gate = s == 8 && std::fmod(ti, 0.29) < 0.01;      // A SEED input: pulses
            previous_seed = seed[i] = STM::ExtractGateFlags(previous_seed, gate);
        }
        gain_control->Process(true, k.gain, false, false, in, float_in, block);

        Parameters p;
        p.time.base = k.time;   p.time.attenuverter = k.time_amt;   p.time.has_cv = false;   p.time.cv = 0.0f;
        p.size.base = k.size;   p.size.attenuverter = k.size_amt;   p.size.has_cv = false;   p.size.cv = 0.0f;
        p.shape.base = k.shape; p.shape.attenuverter = k.shape_amt; p.shape.has_cv = false; p.shape.cv = 0.0f;
        p.pitch.base = k.pitch; p.pitch.attenuverter = k.pitch_amt; p.pitch.has_cv = false; p.pitch.cv = 0.0f;
        p.monophonic_mode = false;
        ModulatedParameter d;
        d.base = k.density;
        d.cv = 0.0f;
        Processor::MixParameters m;
        m.feedback = k.feedback;
        m.dry_wet = k.dry_wet;
        m.reverb_amount = k.reverb;
        m.output_gate = false;
        const SeedButtonState sbs = k.seed_locked ? SEED_BUTTON_STATE_LOCKED
                                                  : (k.seed_button ? SEED_BUTTON_STATE_ON : SEED_BUTTON_STATE_OFF);
        processor->Process(sbs, s == 8 ? seed : nullptr, freeze, d, p, m, float_in, float_out, block);
        for (size_t i = 0; i < block; ++i) {                  // Beads.cc: the output clamped as the codec takes it
            float l = float_out[i].l, r = float_out[i].r;
            CONSTRAIN(l, -1.0f, 0.999999f);
            CONSTRAIN(r, -1.0f, 0.999999f);
            fnv.add(&l, sizeof l);
            fnv.add(&r, sizeof r);
            if (out != nullptr) { out->push_back(l); out->push_back(r); }
        }
    }
    return fnv.h;
}

} // Namespace texture_scenarios
