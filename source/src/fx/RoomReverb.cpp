// SPDX-License-Identifier: GPL-3.0-only
#include "RoomReverb.h"
#include "port/Limiter.h"
#include "port/Reverb.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace usina::fx {

using port::FloatFrame;

struct RoomReverb::Impl {
    virtual ~Impl() = default;
    virtual void reset() = 0;
    virtual void process(float* l, float* r, int n, float amount, int color) = 0;
    virtual int k() const = 0;
    bool active = false;
    bool wetOnly = false;                                // (an AUX: the room alone, its mix at the top)
};

namespace {

template <int K>
struct ImplK final : RoomReverb::Impl {
    using Rv = port::Reverb<K, port::FORMAT_32_BIT>;
    static constexpr int kChunk = 24 * K;                // the module's block: 24 samples at 48 kHz, 0.5 ms

    explicit ImplK(double sampleRate)
        : sr((float)sampleRate), ratio((float)(sampleRate / 48000.0)), memory((size_t)Rv::kBufferSize, 0.0f)
    {
        quietNeeded = (int)(0.25f * sr);
        reverb.Init(memory.data(), sr);
        reset();
    }

    // a damping coefficient of the module's (one pole per sample at 48 kHz) at this rate: the same pole in Hz
    float damping(float c) const { return ratio == 1.0f ? c : 1.0f - std::pow(1.0f - c, 1.0f / ratio); }

    // the module's processor.cc, its reverb's settings from the knob: caveman = every setting but the digital
    void configure(float amount, int color)
    {
        const bool caveman = color != RoomReverb::kClean;
        const float reverb_mix = amount * amount * 0.9f;
        const float reverb_time = amount * amount * amount;
        reverb.set_mix(wetOnly ? 1.0f : reverb_mix);     // (wet only: no dry, the send in full; AMOUNT: the time)
        reverb.set_time(caveman ? (0.45f + 0.5f * reverb_time) : (0.3f + 0.6f * reverb_time));
        reverb.set_lp(caveman ? lpTape : lpClean);
    }

    void reset() override
    {
        reverb.Clear();
        reverb.Reset(sr);
        preLimiter.Init(1.0f / (sr * 4.0f), 0.9f);       // (processor.cc: pre_reverb_limiter_, output_limiter_)
        outLimiter.Init(1.0f / (sr * 6.0f), 0.9f);
        preLimiter.set_rate_ratio(ratio);
        outLimiter.set_rate_ratio(ratio);
        lpClean = damping(0.6f);
        lpTape = damping(0.4f);
        // the reverb's three gains glide to their values over each block, from 0 after Reset (the module's start):
        // one silent block at AMOUNT 0 takes them to their resting values, so the first block played does not
        // fade the voices in
        std::fill(frames, frames + kChunk, FloatFrame { 0.0f, 0.0f });
        configure(0.0f, RoomReverb::kClean);
        reverb.Process(frames, (size_t)kChunk);
        active = false;
        quiet = 0;
    }

    void process(float* l, float* r, int n, float amount, int color) override
    {
        amount = std::clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
        if (!active) {
            if (amount <= 0.0f) {                        // (stopped: the voices untouched, bit for bit -
                if (wetOnly) {                           // or, wet only, nothing: there is no room)
                    std::fill(l, l + n, 0.0f);
                    std::fill(r, r + n, 0.0f);
                }
                return;
            }
            active = true;
            quiet = 0;
        }
        float residue = 0.0f;
        for (int done = 0; done < n;) {
            const int m = std::min(kChunk, n - done);
            for (int i = 0; i < m; ++i) {                // (what goes into the room is always finite)
                const float a = l[done + i], b = r[done + i];
                frames[i] = { std::isfinite(a) ? a : 0.0f, std::isfinite(b) ? b : 0.0f };
                dryL[i] = frames[i].l;
                dryR[i] = frames[i].r;
            }
            preLimiter.Process(frames, (size_t)m);
            configure(amount, color);
            reverb.Process(frames, (size_t)m);
            outLimiter.Process(frames, (size_t)m);
            for (int i = 0; i < m; ++i) {
                l[done + i] = frames[i].l;
                r[done + i] = frames[i].r;
                residue = wetOnly ? std::max(residue, std::max(std::fabs(frames[i].l), std::fabs(frames[i].r)))
                                  : std::max(residue, std::max(std::fabs(frames[i].l - dryL[i]), std::fabs(frames[i].r - dryR[i])));
            }
            done += m;
        }
        if (amount <= 0.0f && residue < 1.0e-6f) {      // the knob at 0 and the tail gone: stop, clean
            quiet += n;
            if (quiet >= quietNeeded) reset();
        } else {
            quiet = 0;
        }
    }

    int k() const override { return K; }

    float sr, ratio;
    std::vector<float> memory;
    Rv reverb;
    port::Limiter preLimiter, outLimiter;
    float lpClean = 0.6f, lpTape = 0.4f;
    FloatFrame frames[kChunk] {};
    float dryL[kChunk] {}, dryR[kChunk] {};
    int quiet = 0, quietNeeded = 0;
};

} // namespace

RoomReverb::RoomReverb() = default;
RoomReverb::~RoomReverb() = default;

void RoomReverb::prepare(double sampleRate)
{
    const double k = sampleRate / 48000.0;               // K: the host's rate rounded to 48 kHz x 1, 2 or 4
    if (k < 1.4) impl = std::make_unique<ImplK<1>>(sampleRate);
    else if (k < 2.8) impl = std::make_unique<ImplK<2>>(sampleRate);
    else impl = std::make_unique<ImplK<4>>(sampleRate);
}

void RoomReverb::reset()
{
    if (impl != nullptr) impl->reset();
}

void RoomReverb::process(float* l, float* r, int n, float amount, int color, bool wetOnly)
{
    if (impl == nullptr) return;
    if (impl->wetOnly != wetOnly) impl->reset();         // (moved between a slot and the AUX: starts over)
    impl->wetOnly = wetOnly;
    impl->process(l, r, n, amount, color);
}

bool RoomReverb::active() const { return impl != nullptr && impl->active; }

int RoomReverb::rateMultiplier() const { return impl != nullptr ? impl->k() : 0; }

} // namespace usina::fx
