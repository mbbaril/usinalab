// SPDX-License-Identifier: GPL-3.0-only
#include "Vapor.h"
#include "dsp/Resampler.h"
#include "dsp/Zeroed.h"
#ifdef _MSC_VER                                          // The port's warnings are upstream's: kept as written
#pragma warning(push, 0)
#endif
#include "fx/granular/dsp/granular_processor.h"
#include "fx/granular/resources.h"
#include "port/dsputil/dsp/dsp.h"
#include "port/dsputil/utils/random.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace usina::fx {

namespace cl = usina_granular;
namespace stm = usina_dsputil;

namespace {
constexpr double kModuleRate = 32000.0;                  // Internal sample rate.
constexpr int kBlock = 32;                               // Internal block size.
constexpr int kChunk = 512;                              // Host samples handled at once (bounds every queue)
constexpr int kPrepares = 4;                             // The main loop's work between two blocks (see runBlock)
constexpr float kTop = 65535.0f / 65536.0f;              // Upper bound prevents lookup-table overrun.
// Map knob center to table unison between indices 477 and 478, not index 512.
// Use two linear segments to preserve the table endpoints at +/-24 semitones.
float pitchPosition(float knob)
{
    constexpr float kUnison = 477.5f / 1024.0f;
    knob = std::clamp(knob, 0.0f, 1.0f);
    const float x = knob < 0.5f ? knob * 2.0f * kUnison : kUnison + (knob - 0.5f) * 2.0f * (1.0f - kUnison);
    return std::min(x, kTop);
}

// Block smoothing indexed by Vapor::Knob. Reusing mode enum values here
// can silently address the wrong control.
constexpr int kNumPots = Vapor::kNumKnobs;
constexpr float kSmoothing[kNumPots] = { 0.05f, 0.01f, 0.01f, 0.01f, 0.01f, 0.05f, 0.05f, 0.05f, 0.05f };

// Stereo frames, first in first out; linear, compacted when the end is reached
struct Fifo {
    std::vector<float> l, r;
    int head = 0, size = 0;
    void prepare(int cap)
    {
        l.assign((size_t)cap, 0.0f);
        r.assign((size_t)cap, 0.0f);
        head = size = 0;
    }
    void clear() { head = size = 0; }
    void push(float a, float b)
    {
        if (head + size >= (int)l.size()) {
            std::copy(l.begin() + head, l.begin() + head + size, l.begin());
            std::copy(r.begin() + head, r.begin() + head + size, r.begin());
            head = 0;
        }
        if (size >= (int)l.size()) return;              // Full: cannot happen with the sizes prepared
        l[(size_t)(head + size)] = a;
        r[(size_t)(head + size)] = b;
        ++size;
    }
    void pop(int n)
    {
        head += n;
        size -= n;
        if (size == 0) head = 0;
    }
};

short toCodec(float x)                                   // The codec's 16 bits: the host's full scale is its
{
    return (short)std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f);
}
} // Namespace

struct Vapor::Impl {
    double hostRate = 48000.0;
    std::vector<uint8_t> blockMem = std::vector<uint8_t>(118784);        // Processor memory regions.
    std::vector<uint8_t> blockCcm = std::vector<uint8_t>(65536 - 128);
    usina::Zeroed<cl::GranularProcessor> processor;
    uint32_t rng = 0x21;                                 // Deterministic DSP random seed.
    float pots[kNumPots] {};
    // LIMIT (Vapor.h): the input's held power, the fed-back signal's ceiling (inside the module)
    double inHeld = 0.0;
    static constexpr double kHeldFall = 0.99923;         // A block is 1 ms at 32 kHz: 10 dB in power over ~3 s

    bool direct = false;
    usina::Resampler down, up;                           // Host -> module, module -> host
    int prefill = 0;                                     // Module samples of silence ahead of the output
    double latencyHost = 0.0, upStart = 0.0;

    Fifo inQ, outQ;                                      // At the module's rate
    std::vector<uint8_t> gateRing = std::vector<uint8_t>(1 << 16), gateQ = std::vector<uint8_t>(1 << 16);
    int64_t hostWritten = 0, moduleIn = 0, gateRead = 0;
    bool previousGate = false;
    std::vector<float> tl = std::vector<float>(4 * kChunk), tr = std::vector<float>(4 * kChunk);

    int mode = kGranular, quality = kStereo16;
    bool active = false;
    float fade = 0.0f, fadeOnOff = 0.0f, fadeSwitch = 0.0f;

    void prepare(double fs)
    {
        hostRate = fs;
        if (processor == nullptr) processor = usina::makeZeroed<cl::GranularProcessor>();
        direct = std::fabs(kModuleRate - fs) < 0.5;
        if (direct) {
            prefill = kBlock;
            latencyHost = kBlock;
        } else {
            down.prepare(fs, kModuleRate, kChunk);
            const int moduleChunk = (int)std::ceil(kChunk * kModuleRate / fs) + 2 * kBlock + 8;
            up.prepare(kModuleRate, fs, moduleChunk + 4096);
            // The output must never wait: the converters' half-widths and one block ahead (module samples); the way
            // back starts a fraction of a sample early so that the whole delay is whole host samples
            prefill = (int)std::ceil(up.halfWidth() + kBlock + down.halfWidth() * kModuleRate / fs) + 2;
            latencyHost = std::ceil(prefill * fs / kModuleRate);
            upStart = prefill - latencyHost * kModuleRate / fs;
        }
        const int cap = (int)std::ceil(kChunk * kModuleRate / std::min(fs, 22050.0)) + 4096;
        inQ.prepare(cap);
        outQ.prepare(cap);
        fadeOnOff = (float)(1.0 / (0.020 * fs));
        fadeSwitch = (float)(1.0 / (0.010 * fs));
        active = false;
        fade = 0.0f;
    }

    void restartStreams()
    {
        inQ.clear();
        outQ.clear();
        hostWritten = moduleIn = gateRead = 0;
        std::fill(gateRing.begin(), gateRing.end(), 0);
        std::fill(gateQ.begin(), gateQ.end(), 0);
        if (direct) {
            for (int i = 0; i < prefill; ++i) outQ.push(0.0f, 0.0f);
        } else {
            down.reset();
            up.reset(upStart);
            std::fill(tl.begin(), tl.end(), 0.0f);
            for (int left = prefill; left > 0; left -= (int)tl.size()) {
                const int m = std::min(left, (int)tl.size());
                up.push(tl.data(), tl.data(), m);
            }
        }
    }

    // The mode and quality, as the module's UI sets them (the processor restarts at its next Prepare)
    void configure(const Vapor::Knobs& k)
    {
        mode = std::clamp(k.mode, 0, (int)kNumModes - 1);
        quality = std::clamp(k.quality, 0, (int)kNumQualities - 1);
        stm::Random::Scope scope(&rng);
        processor->set_playback_mode((cl::PlaybackMode)mode);
        processor->set_quality(quality);
    }

    // Reconstruct in zeroed storage with settled controls.
    void start(const Vapor::Knobs& k)
    {
        usina::rezero(processor);
        std::fill(blockMem.begin(), blockMem.end(), 0);
        std::fill(blockCcm.begin(), blockCcm.end(), 0);
        rng = 0x21;
        {
            stm::Random::Scope scope(&rng);
            processor->Init(blockMem.data(), blockMem.size(), blockCcm.data(), blockCcm.size());
        }
        configure(k);
        const float v[kNumPots] = { k.position, k.size, k.pitch, k.density, k.texture, k.dryWet, k.spread,
                                    k.feedback, k.reverb };
        for (int i = 0; i < kNumPots; ++i) pots[i] = std::clamp(v[i], 0.0f, 1.0f);   // Settled where the knobs are
        previousGate = false;
        inHeld = 0.0;                                    // LIMIT: from rest
        restartStreams();
    }

    // Complete pending splice and spectral work before processing the next block.
    // Prepare returns immediately when no background work remains.
    void runBlock(const Vapor::Knobs& k)
    {
        stm::Random::Scope scope(&rng);
        const float v[kNumPots] = { k.position, k.size, k.pitch, k.density, k.texture, k.dryWet, k.spread,
                                    k.feedback, k.reverb };
        for (int i = 0; i < kNumPots; ++i) pots[i] += kSmoothing[i] * (std::clamp(v[i], 0.0f, 1.0f) - pots[i]);
        const auto knob = [&](int i) { return std::min(pots[i], kTop); };
        cl::Parameters* p = processor->mutable_parameters();
        p->position = knob(kPosition);
        p->size = knob(kSize);
        p->density = knob(kDensity);
        p->texture = knob(kTexture);
        p->dry_wet = std::clamp(knob(kDryWet) * 1.05f - 0.025f, 0.0f, kTop);
        p->stereo_spread = knob(kSpread);
        p->feedback = knob(kFeedback);
        p->reverb = knob(kReverbKnob);
        p->pitch = stm::Interpolate(cl::lut_quantized_pitch, pitchPosition(pots[kPitch]), 1024.0f);   // -24 .. +24
        p->freeze = k.freeze;
        p->granular.reverse = k.reverse;
        // The TRIG input: a rising edge in the block, and where the gate is at its end
        bool rising = false, gate = previousGate;
        for (int i = 0; i < kBlock; ++i) {
            const bool g = gateQ[(size_t)((gateRead + i) & (int64_t)(gateQ.size() - 1))] != 0;
            rising = rising || (g && !gate);
            gate = g;
        }
        gateRead += kBlock;
        previousGate = gate;
        p->trigger = rising;
        p->gate = gate;

        cl::ShortFrame in[kBlock], out[kBlock];
        for (int i = 0; i < kBlock; ++i) {
            in[i].l = toCodec(inQ.l[(size_t)(inQ.head + i)]);
            in[i].r = toCodec(inQ.r[(size_t)(inQ.head + i)]);
        }
        inQ.pop(kBlock);
        // Hold input power with approximately 10 dB decay over 3 s.
        // Apply the internal +6 dB feedback ceiling when LIMIT is enabled; zero disables it.
        double pin = 0.0;
        for (int i = 0; i < kBlock; ++i) {
            const double a = in[i].l / 32768.0, b = in[i].r / 32768.0;
            pin += 0.5 * (a * a + b * b);
        }
        inHeld = std::max(pin / kBlock, inHeld * kHeldFall);
        // The module injects the fed-back signal x 1.4: its power ceiling half the input's, what goes in near its level
        p->usina_fb_ceiling = k.limit ? (float)std::max(0.5 * inHeld, 1.0e-12) : 0.0f;
        for (int i = 0; i < kPrepares; ++i) processor->Prepare();
        processor->Process(in, out, kBlock);
        for (int i = 0; i < kBlock; ++i) outQ.push(out[i].l / 32768.0f, out[i].r / 32768.0f);
    }

    void chunk(float* l, float* r, int n, const Vapor::Knobs& k, const bool* gate, bool on)
    {
        // 1. the host's samples into the module's rate (the TRIG gate taken at each module sample's own time)
        for (int i = 0; i < n; ++i)
            gateRing[(size_t)((hostWritten + i) & (int64_t)(gateRing.size() - 1))] = gate != nullptr && gate[i] ? 1 : 0;
        const auto gateAt = [&](int64_t moduleSample) {
            const int64_t h = direct ? moduleSample : (int64_t)std::llround((double)moduleSample * hostRate / kModuleRate);
            return gateRing[(size_t)(h & (int64_t)(gateRing.size() - 1))];
        };
        if (direct) {
            for (int i = 0; i < n; ++i) {
                gateQ[(size_t)((moduleIn + i) & (int64_t)(gateQ.size() - 1))] = gateAt(moduleIn + i);
                inQ.push(l[i], r[i]);
            }
            moduleIn += n;
        } else {
            down.push(l, r, n);
            while (const int a = std::min(down.available(), (int)tl.size())) {
                down.pull(tl.data(), tr.data(), a);
                for (int i = 0; i < a; ++i) {
                    gateQ[(size_t)((moduleIn + i) & (int64_t)(gateQ.size() - 1))] = gateAt(moduleIn + i);
                    inQ.push(tl[(size_t)i], tr[(size_t)i]);
                }
                moduleIn += a;
            }
        }
        hostWritten += n;
        // 2. the module, a block at a time
        while (inQ.size >= kBlock) runBlock(k);
        // 3. back to the host's rate, then 4. faded with the input as it came (the switch, a mode or quality change)
        const bool change = std::clamp(k.mode, 0, (int)kNumModes - 1) != mode ||
                            std::clamp(k.quality, 0, (int)kNumQualities - 1) != quality;
        if (direct) {
            const int m = std::min(n, outQ.size);
            for (int i = 0; i < n; ++i) {
                tl[(size_t)i] = i < m ? outQ.l[(size_t)(outQ.head + i)] : 0.0f;
                tr[(size_t)i] = i < m ? outQ.r[(size_t)(outQ.head + i)] : 0.0f;
            }
            outQ.pop(m);
        } else {
            if (outQ.size > 0) {
                up.push(outQ.l.data() + outQ.head, outQ.r.data() + outQ.head, outQ.size);
                outQ.pop(outQ.size);
            }
            const int a = std::min(n, up.available());
            up.pull(tl.data(), tr.data(), a);
            for (int i = a; i < n; ++i) tl[(size_t)i] = tr[(size_t)i] = 0.0f;
        }
        const float target = on && !change ? 1.0f : 0.0f;
        const float step = change ? fadeSwitch : fadeOnOff;
        for (int i = 0; i < n; ++i) {
            fade = target > fade ? std::min(target, fade + step) : std::max(target, fade - step);
            l[i] += (tl[(size_t)i] - l[i]) * fade;
            r[i] += (tr[(size_t)i] - r[i]) * fade;
        }
        if (fade <= 0.0f) {
            if (change && on) {                          // Faded to the plain input: switch, then fade back in
                configure(k);
                restartStreams();
            } else if (!on) {
                active = false;
            }
        }
    }
};

Vapor::Vapor() : impl(std::make_unique<Impl>()) {}
Vapor::~Vapor() = default;

void Vapor::prepare(double hostRate, int) { impl->prepare(hostRate); }

void Vapor::reset()
{
    impl->active = false;
    impl->fade = 0.0f;
}

void Vapor::process(float* l, float* r, int n, const Knobs& k, const bool* gate, bool on)
{
    auto& s = *impl;
    if (!s.active) {
        if (!on) return;                                 // Off: the input untouched, bit for bit
        s.start(k);
        s.active = true;
    }
    for (int done = 0; done < n && s.active;) {
        const int m = std::min(kChunk, n - done);
        s.chunk(l + done, r + done, m, k, gate != nullptr ? gate + done : nullptr, on);
        done += m;
    }
}

bool Vapor::active() const { return impl->active; }

int Vapor::latencySamples() const { return (int)std::lround(impl->latencyHost); }

const char* Vapor::modeName(int m)
{
    static const char* const kNames[kNumModes] = { "GRANULAR", "STRETCH", "LOOP DELAY", "SPECTRAL", "REVERB",
                                                   "RESONATOR" };
    return kNames[std::clamp(m, 0, (int)kNumModes - 1)];
}

const char* Vapor::qualityName(int q)
{
    static const char* const kNames[kNumQualities] = { "STEREO HI", "MONO HI", "STEREO LO-FI", "MONO LO-FI" };
    return kNames[std::clamp(q, 0, (int)kNumQualities - 1)];
}

float Vapor::pitchSemitones(float knob)
{
    return stm::Interpolate(cl::lut_quantized_pitch, pitchPosition(knob), 1024.0f);
}

const char* Vapor::knobName(int mode, int knob)
{
    // Granular_processor.cc: what each mode reads each parameter as
    static const char* const kNames[kNumModes][kNumKnobs] = {
        { "POSITION", "SIZE", "PITCH", "DENSITY", "TEXTURE", "DRY/WET", "SPREAD", "FEEDBACK", "REVERB" },
        { "POSITION", "SIZE", "PITCH", "DIFFUSION", "FILTER", "DRY/WET", "SPREAD", "FEEDBACK", "REVERB" },
        { "TIME", "SIZE", "PITCH", "DIFFUSION", "FILTER", "DRY/WET", "SPREAD", "FEEDBACK", "REVERB" },
        { "POSITION", "WARP", "PITCH", "REFRESH", "QUANTIZE", "DRY/WET", "SPREAD", "FEEDBACK", "REVERB" },
        { "PREDELAY", "SIZE", "PITCH", "DECAY", "FILTER", "DRY/WET", "DIFFUSION", "MOD RATE", "MOD DEPTH" },
        { "BURST", "CHORD", "PITCH", "FEEDBACK", "DAMPING", "DISTORTION", "STEREO", "HARMONICS", "SCATTER" },
    };
    return kNames[std::clamp(mode, 0, (int)kNumModes - 1)][std::clamp(knob, 0, (int)kNumKnobs - 1)];
}

} // Namespace usina::fx
