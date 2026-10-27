// SPDX-License-Identifier: GPL-3.0-only
#include "Texture.h"
#include "dsp/Resampler.h"
#include "dsp/Zeroed.h"
#ifdef _MSC_VER                                          // (the port's warnings are upstream's: kept as written)
#pragma warning(push, 0)
#endif
#include "fx/texture/dsp/processor.h"
#include "fx/texture/gain_control.h"
#include "fx/texture/pot_channel.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace usina::fx {

namespace tx = usina_texture;
namespace stm = usina_stmlib;

namespace {
// The module's processor and gain control are globals there: made in zeroed memory (dsp/Zeroed.h, which says why)
using usina::Zeroed;
using usina::makeZeroed;
using usina::rezero;

constexpr int kBlockRate = 4000;                         // (beads.cc: the codec's block rate)
constexpr int kChunk = 512;                              // host samples handled at once (bounds every queue)
constexpr double kModuleRates[] = { 24000.0, 32000.0, 48000.0 };

enum Pot { kTime, kSize, kShape, kPitch, kTimeAmt, kSizeAmt, kShapeAmt, kPitchAmt, kDensity, kFeedback, kDryWet,
           kReverb, kNumPots };

// stereo frames and the SEED gate, first in first out; linear, compacted when the end is reached
struct Fifo {
    std::vector<float> l, r;
    std::vector<uint8_t> g;
    int head = 0, size = 0;
    void prepare(int cap)
    {
        l.assign((size_t)cap, 0.0f);
        r.assign((size_t)cap, 0.0f);
        g.assign((size_t)cap, 0);
        head = size = 0;
    }
    void clear() { head = size = 0; }
    void push(float a, float b, bool gate)
    {
        if (head + size >= (int)l.size()) {
            std::copy(l.begin() + head, l.begin() + head + size, l.begin());
            std::copy(r.begin() + head, r.begin() + head + size, r.begin());
            std::copy(g.begin() + head, g.begin() + head + size, g.begin());
            head = 0;
        }
        if (size >= (int)l.size()) return;              // (full: cannot happen with the sizes prepared)
        l[(size_t)(head + size)] = a;
        r[(size_t)(head + size)] = b;
        g[(size_t)(head + size)] = gate ? 1 : 0;
        ++size;
    }
    void pop(int n)
    {
        head += n;
        size -= n;
        if (size == 0) head = 0;
    }
};

// one module rate: the converters to and from the host's (or none: the same rate)
struct RatePath {
    double rate = 48000.0;
    bool direct = true;
    usina::Resampler down, up;                           // host -> module, module -> host
    int prefill = 0;                                     // module samples of silence ahead of the output
    double latencyHost = 0.0;                            // (whole host samples)
    double upStart = 0.0;                                // the way back's first position (module samples, <= 0)
};
} // namespace

struct Texture::Impl {
    double hostRate = 48000.0;
    std::vector<uint8_t> bank0 = std::vector<uint8_t>(512 * 1024), bank2 = std::vector<uint8_t>(256 * 1024);
    std::vector<uint16_t> reverbMemory = std::vector<uint16_t>(32768);
    Zeroed<tx::Processor> processor;
    Zeroed<tx::GainControl> gainControl;
    tx::PotChannel pots[kNumPots];
    uint32_t rng = 0x21;                                 // (the processor's own random generator: stmlib's seed)
    stm::GateFlags previousSeed = stm::GATE_FLAG_LOW;

    RatePath paths[3];
    RatePath* path = &paths[2];
    int media = Texture::kDigital;
    bool mono = false;
    int block = 12;

    Fifo inQ, outQ;                                      // at the module's rate
    std::vector<uint8_t> gateRing = std::vector<uint8_t>(1 << 16);
    int64_t hostWritten = 0, moduleIn = 0;
    std::vector<float> tl = std::vector<float>(4 * kChunk), tr = std::vector<float>(4 * kChunk);

    bool active = false;
    float fade = 0.0f, fadeOnOff = 0.0f, fadeSwitch = 0.0f;

    void prepare(double fs)
    {
        hostRate = fs;
        if (processor == nullptr) {
            processor = makeZeroed<tx::Processor>();
            gainControl = makeZeroed<tx::GainControl>();
        }
        for (int i = 0; i < 3; ++i) {
            auto& p = paths[i];
            p.rate = kModuleRates[i];
            p.direct = std::fabs(p.rate - fs) < 0.5;
            const int blk = (int)p.rate / kBlockRate;
            if (p.direct) {
                p.prefill = blk;
                p.latencyHost = blk;
            } else {
                p.down.prepare(fs, p.rate, kChunk);
                const int moduleChunk = (int)std::ceil(kChunk * p.rate / fs) + 2 * blk + 8;
                p.up.prepare(p.rate, fs, moduleChunk + 4096);
                // the output must never wait: the converters' half-widths and one block ahead (module samples); the
                // way back starts a fraction of a sample early so that the whole delay is whole host samples
                p.prefill = (int)std::ceil(p.up.halfWidth() + blk + p.down.halfWidth() * p.rate / fs) + 2;
                p.latencyHost = std::ceil(p.prefill * fs / p.rate);
                p.upStart = p.prefill - p.latencyHost * p.rate / fs;
            }
        }
        const int cap = (int)std::ceil(kChunk * 48000.0 / std::min(fs, 24000.0)) + 4096;
        inQ.prepare(cap);
        outQ.prepare(cap);
        fadeOnOff = (float)(1.0 / (0.020 * fs));
        fadeSwitch = (float)(1.0 / (0.010 * fs));
        active = false;
        fade = 0.0f;
    }

    static tx::GainControl::SoftClippingMode clipping(int m)
    {
        return m == Texture::kDirtyTape ? tx::GainControl::SOFT_CLIPPING_MODE_ON
                                        : (m == Texture::kCleanTape ? tx::GainControl::SOFT_CLIPPING_MODE_DYNAMIC
                                                                    : tx::GainControl::SOFT_CLIPPING_MODE_OFF);
    }

    static uint16_t raw(float v) { return (uint16_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 65535.0f); }

    void readPots(const Texture::Knobs& k)               // (cv_reader.Read: every pot, once a block)
    {
        const float v[kNumPots] = { k.time, k.size, k.shape, k.pitch, k.timeAmt, k.sizeAmt, k.shapeAmt, k.pitchAmt,
                                    k.density, k.feedback, k.dryWet, k.reverb };
        for (int i = 0; i < kNumPots; ++i) pots[i].Process(raw(v[i]));
    }

    // the streams aligned again at a (new) module rate: queues emptied, the output's lead of silence put back
    void restartStreams()
    {
        const int index = media == Texture::kDirtyTape ? 0 : (media == Texture::kVintage ? 1 : 2);
        path = &paths[index];
        block = (int)path->rate / kBlockRate;
        inQ.clear();
        outQ.clear();
        hostWritten = moduleIn = 0;
        std::fill(gateRing.begin(), gateRing.end(), 0);
        if (path->direct) {
            for (int i = 0; i < path->prefill; ++i) outQ.push(0.0f, 0.0f, false);
        } else {
            path->down.reset();
            path->up.reset(path->upStart);
            std::fill(tl.begin(), tl.end(), 0.0f);
            for (int left = path->prefill; left > 0; left -= (int)tl.size()) {
                const int m = std::min(left, (int)tl.size());
                path->up.push(tl.data(), tl.data(), m);
            }
        }
    }

    // turned on: empty, its knobs where they are, at its media (beads.cc Init and ConfigureAndStartCodec, without
    // the codec's restart silence - the hardware's, not the processor's). Made anew in zeroed memory, as at power-up
    void start(const Texture::Knobs& k)
    {
        rezero(processor);
        rezero(gainControl);
        media = std::clamp(k.media, 0, (int)Texture::kNumMedia - 1);
        mono = k.mono;
        uint8_t* buffers[3] = { bank0.data(), bank0.data() + 256 * 1024, bank2.data() };
        rng = 0x21;
        {
            stm::Random::Scope scope(&rng);
            processor->Init(buffers, reverbMemory.data());
            processor->Configure(tx::Processor::Media(media), mono);
        }
        gainControl->Init(processor->sample_rate(), clipping(media), true);
        for (int i = 0; i < kNumPots; ++i) {             // (cv_reader.cc Init: the laws, the smoothing, TIME's hysteresis)
            const auto law = i >= kTimeAmt && i <= kPitchAmt ? tx::PotChannel::LAW_ATTENUVERTER
                                                             : (i == kPitch ? tx::PotChannel::LAW_DETUNE : tx::PotChannel::LAW_NORMAL);
            pots[i].Init(law, 0.05f, i == kTime ? 0.001f : 0.0f);
        }
        for (int j = 0; j < 300; ++j) readPots(k);       // (settled where the knobs are: the module's are, at power-up)
        previousSeed = stm::GATE_FLAG_LOW;
        restartStreams();
    }

    // the media or mono changed (faded out first): the module's Configure, its gain control told, the new rate
    void reconfigure(const Texture::Knobs& k)
    {
        media = std::clamp(k.media, 0, (int)Texture::kNumMedia - 1);
        mono = k.mono;
        {
            stm::Random::Scope scope(&rng);
            processor->Configure(tx::Processor::Media(media), mono);
        }
        gainControl->Init(processor->sample_rate(), clipping(media), false);
        restartStreams();
    }

    // one block of the module (beads.cc Process): the knobs, the gain control, the processor, the clamp
    void runBlock(const Texture::Knobs& k)
    {
        stm::Random::Scope scope(&rng);
        readPots(k);
        const int n = block;
        tx::FloatFrame in[tx::kMaxCodecBlockSize], fin[tx::kMaxCodecBlockSize], fout[tx::kMaxCodecBlockSize];
        bool freeze[tx::kMaxCodecBlockSize];
        stm::GateFlags seed[tx::kMaxCodecBlockSize];
        for (int i = 0; i < n; ++i) {
            in[i].l = inQ.l[(size_t)(inQ.head + i)];
            in[i].r = inQ.r[(size_t)(inQ.head + i)];
            freeze[i] = k.freeze;
            previousSeed = seed[i] = stm::ExtractGateFlags(previousSeed, inQ.g[(size_t)(inQ.head + i)] != 0);
        }
        inQ.pop(n);
        const uint8_t gainSetting = k.gain <= 0.0f ? 0 : (uint8_t)(1 + std::lround(std::clamp(k.gain, 0.0f, 1.0f) * 254.0f));
        gainControl->Process(true, gainSetting, false, false, in, fin, (size_t)n);

        tx::Parameters p;
        const auto rp = [](tx::RandomizedParameter& q, float base, float amount) {
            q.base = base;
            q.attenuverter = amount;
            q.has_cv = false;
            q.cv = 0.0f;
        };
        rp(p.time, pots[kTime].value(), pots[kTimeAmt].value());
        rp(p.size, pots[kSize].value(), pots[kSizeAmt].value());
        rp(p.shape, pots[kShape].value(), pots[kShapeAmt].value());
        rp(p.pitch, pots[kPitch].value(), pots[kPitchAmt].value());
        p.monophonic_mode = false;
        tx::ModulatedParameter d;
        d.base = pots[kDensity].value();
        d.cv = 0.0f;
        tx::Processor::MixParameters m;
        const auto mix = [&](int pot) { return std::clamp(pots[pot].value() * 1.005f, 0.0f, 1.0f); };
        m.feedback = mix(kFeedback);
        m.dry_wet = mix(kDryWet);
        m.reverb_amount = mix(kReverb);
        m.output_gate = false;

        tx::SeedButtonState button = tx::SEED_BUTTON_STATE_LOCKED;   // (the module's default: latched)
        const stm::GateFlags* seedInput = nullptr;
        switch (k.seedMode) {
            case Texture::kSeedNotes: button = tx::SEED_BUTTON_STATE_OFF; seedInput = seed; break;
            case Texture::kSeedNotesDivided:
            case Texture::kSeedHostClock: seedInput = seed; break;
            default: break;
        }
        processor->Process(button, seedInput, freeze, d, p, m, fin, fout, (size_t)n);
        for (int i = 0; i < n; ++i) {                    // (the codec takes -1 .. 0.999999)
            outQ.push(std::clamp(fout[i].l, -1.0f, 0.999999f), std::clamp(fout[i].r, -1.0f, 0.999999f), false);
        }
    }

    void chunk(float* l, float* r, int n, const Texture::Knobs& k, const bool* gate, bool on)
    {
        // 1. the host's samples into the module's rate (the SEED gate taken at each module sample's own time)
        for (int i = 0; i < n; ++i)
            gateRing[(size_t)((hostWritten + i) & (int64_t)(gateRing.size() - 1))] = gate != nullptr && gate[i] ? 1 : 0;
        if (path->direct) {
            for (int i = 0; i < n; ++i) inQ.push(l[i], r[i], gate != nullptr && gate[i]);
            moduleIn += n;
        } else {
            path->down.push(l, r, n);
            while (const int a = std::min(path->down.available(), (int)tl.size())) {
                path->down.pull(tl.data(), tr.data(), a);
                for (int i = 0; i < a; ++i) {
                    const int64_t h = (int64_t)std::llround((double)(moduleIn + i) * hostRate / path->rate);
                    inQ.push(tl[(size_t)i], tr[(size_t)i], gateRing[(size_t)(h & (int64_t)(gateRing.size() - 1))] != 0);
                }
                moduleIn += a;
            }
        }
        hostWritten += n;
        // 2. the module, a block at a time
        while (inQ.size >= block) runBlock(k);
        // 3. back to the host's rate, then 4. faded with the voices as they came (the switch, a media change)
        const bool change = k.media != media || k.mono != mono;
        if (path->direct) {
            const int m = std::min(n, outQ.size);
            for (int i = 0; i < n; ++i) {
                tl[(size_t)i] = i < m ? outQ.l[(size_t)(outQ.head + i)] : 0.0f;
                tr[(size_t)i] = i < m ? outQ.r[(size_t)(outQ.head + i)] : 0.0f;
            }
            outQ.pop(m);
        } else {
            if (outQ.size > 0) {
                path->up.push(outQ.l.data() + outQ.head, outQ.r.data() + outQ.head, outQ.size);
                outQ.pop(outQ.size);
            }
            const int a = std::min(n, path->up.available());
            path->up.pull(tl.data(), tr.data(), a);
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
            if (change && on) reconfigure(k);            // (faded to the plain voices: switch, then fade back in)
            else if (!on) active = false;
        }
    }
};

Texture::Texture() : impl(std::make_unique<Impl>()) {}
Texture::~Texture() = default;

void Texture::prepare(double hostRate, int) { impl->prepare(hostRate); }

void Texture::reset()
{
    impl->active = false;
    impl->fade = 0.0f;
}

void Texture::process(float* l, float* r, int n, const Knobs& k, const bool* gate, bool on)
{
    auto& s = *impl;
    if (!s.active) {
        if (!on) return;                                 // (off: the voices untouched, bit for bit)
        s.start(k);
        s.active = true;
    }
    for (int done = 0; done < n && s.active;) {
        const int m = std::min(kChunk, n - done);
        s.chunk(l + done, r + done, m, k, gate != nullptr ? gate + done : nullptr, on);
        done += m;
    }
}

bool Texture::active() const { return impl->active; }

int Texture::latencySamples() const { return (int)std::lround(impl->path->latencyHost); }

} // namespace usina::fx
