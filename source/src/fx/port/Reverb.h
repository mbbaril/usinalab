// ---------------------------------------------------------------------------
// This file ports code from Emilie Gillet's Eurorack firmware (https://github.com/pichenettes/eurorack), used under
// the MIT licence. The upstream files it derives from: beads/dsp/fx/reverb.h ("Reverb.") and beads/dsp/frame.h (the
// float frame), read at eurorack f4cfa76 (D:/upstream/eurorack), ported line for line.
//
//   Copyright 2014 Emilie Gillet.
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//   THE SOFTWARE.
//
// The Usina as a whole, including this port, is licensed under the GPL-3.0 - see LICENSE at the repository root.
// See also THIRD_PARTY.md.
// ---------------------------------------------------------------------------
// What changed from upstream, and only this:
//  - the namespace (usina::port); TAIL written out (", -1" is the "-1" offset argument below);
//  - two template parameters. K, an integer rate multiplier: every delay length, read/write offset and modulation
//    depth in samples is multiplied by K (and the engine's LFO update period, FxEngine.h), so the same room (in
//    seconds) runs at 48 kHz x K. The author did the same
//    rescaling by hand from the 32 kHz original to this 48 kHz one. format, the engine's memory format: upstream
//    fixes FORMAT_16_BIT (a 16-bit buffer on the module); the plugin uses FORMAT_32_BIT, the engine's float option.
//    At K = 1 and FORMAT_16_BIT this is upstream's reverb, and tests/PortTest checks it bit for bit against it;
//  - the members zero-initialised (upstream leaves reverb_time_ to the first set_time, which always comes first).
#pragma once
#include "FxEngine.h"
#include <cstddef>

namespace usina::port {

// beads/dsp/frame.h (Frame<float>, the part the reverb and the limiter use)
struct FloatFrame {
    float l;
    float r;
};

template <int K = 1, Format format = FORMAT_16_BIT>
class Reverb {
public:
    typedef FxEngine<32768 * K, format, K> E;
    typedef typename E::T T;
    enum { kBufferSize = 32768 * K };

    Reverb() {}
    ~Reverb() {}
    Reverb(const Reverb&) = delete;
    void operator=(const Reverb&) = delete;

    void Init(T* buffer, float sample_rate)
    {
        engine_.Init(buffer);
        Reset(sample_rate);
    }

    void Reset(float sample_rate)
    {
        engine_.SetLFOFrequency(LFO_1, 0.5f / sample_rate);
        engine_.SetLFOFrequency(LFO_2, 0.3f / sample_rate);
        lp_ = 0.7f;
        lp_1_ = 0.0f;
        lp_2_ = 0.0f;
        mix_ = 0.0f;
        dry_ = 0.0f;
        wet_ = 0.0f;
        send_ = 0.0f;
    }

    void Process(FloatFrame* in_out, size_t size)
    {
        // This is the Griesinger topology described in the Dattorro paper
        // (4 AP diffusers on the input, then a loop of 2x 2AP+1Delay).
        // Modulation is applied in the loop of the first diffuser AP for additional
        // smearing; and to the two long delays for a slow shimmer/chorus effect.
        typedef typename E::template Reserve<150 * K,
            typename E::template Reserve<214 * K,
            typename E::template Reserve<319 * K,
            typename E::template Reserve<527 * K,
            typename E::template Reserve<2182 * K,
            typename E::template Reserve<2690 * K,
            typename E::template Reserve<4501 * K,
            typename E::template Reserve<2525 * K,
            typename E::template Reserve<2197 * K,
            typename E::template Reserve<6312 * K>>>>>>>>>> Memory;
        typename E::template DelayLine<Memory, 0> ap1;
        typename E::template DelayLine<Memory, 1> ap2;
        typename E::template DelayLine<Memory, 2> ap3;
        typename E::template DelayLine<Memory, 3> ap4;
        typename E::template DelayLine<Memory, 4> dap1a;
        typename E::template DelayLine<Memory, 5> dap1b;
        typename E::template DelayLine<Memory, 6> del1;
        typename E::template DelayLine<Memory, 7> dap2a;
        typename E::template DelayLine<Memory, 8> dap2b;
        typename E::template DelayLine<Memory, 9> del2;
        typename E::Context c;

        const float kap = 0.625f;
        const float klp = lp_;
        const float krt = reverb_time_;

        const float input_gain = 0.2f * (1.0f - 0.333f * reverb_time_);
        const float output_gain = 0.4f / input_gain;

        // const float send_amount = 1.0f;
        // const float wet = mix_ * (2.0f - mix_) * output_gain;

        const float send_amount_sqrt = mix_ * (2.0f - mix_);
        const float send_amount = send_amount_sqrt * (2.0f - send_amount_sqrt);
        const float wet = output_gain;
        const float dry = (1.0f - mix_) * (1.0f + mix_);

        ParameterInterpolator wet_modulation(&wet_, wet, size);
        ParameterInterpolator dry_modulation(&dry_, dry, size);
        ParameterInterpolator send_modulation(&send_, send_amount * input_gain, size);

        while (size--) {
            const float dry_amount = dry_modulation.Next();
            const float wet_amount = wet_modulation.Next();
            const float send_amount = send_modulation.Next();

            float wet;
            float apout = 0.0f;
            engine_.Start(&c);

            // Smear AP1 inside the loop.
            c.Interpolate(ap1, 10.0f * K, LFO_1, 80.0f * K, 1.0f);
            c.Write(ap1, 100 * K, 0.0f);

            c.Read(in_out->l + in_out->r, send_amount);

            // Diffuse through 4 allpasses.
            c.Read(ap1, -1, kap);
            c.WriteAllPass(ap1, -kap);
            c.Read(ap2, -1, kap);
            c.WriteAllPass(ap2, -kap);
            c.Read(ap3, -1, kap);
            c.WriteAllPass(ap3, -kap);
            c.Read(ap4, -1, kap);
            c.WriteAllPass(ap4, -kap);
            c.Write(apout);

            // Main reverb loop.
            c.Load(apout);
            c.Interpolate(del2, 6261.0f * K, LFO_2, 50.0f * K, krt);
            c.Lp(lp_1_, klp);
            c.Read(dap1a, -1, -kap);
            c.WriteAllPass(dap1a, kap);
            c.Read(dap1b, -1, kap);
            c.WriteAllPass(dap1b, -kap);
            c.Write(del1, 1.0f);
            c.Write(wet);

            in_out->l = in_out->l * dry_amount + wet * wet_amount;

            c.Load(apout);
            c.Interpolate(del1, 4460.0f * K, LFO_1, 40.0f * K, krt);
            c.Lp(lp_2_, klp);
            c.Read(dap2a, -1, kap);
            c.WriteAllPass(dap2a, -kap);
            c.Read(dap2b, -1, -kap);
            c.WriteAllPass(dap2b, kap);
            c.Write(del2, 1.0f);
            c.Write(wet);

            in_out->r = in_out->r * dry_amount + wet * wet_amount;
            ++in_out;
        }
    }

    inline void set_mix(float mix) { mix_ = mix; }

    inline void set_time(float reverb_time) { reverb_time_ = reverb_time; }

    inline void set_lp(float lp) { lp_ = lp; }

    inline void Clear() { engine_.Clear(); }

private:
    E engine_;

    float mix_ = 0.0f;
    float wet_ = 0.0f;
    float dry_ = 0.0f;
    float send_ = 0.0f;
    float reverb_time_ = 0.0f;
    float lp_ = 0.0f;

    float lp_1_ = 0.0f;
    float lp_2_ = 0.0f;
};

} // namespace usina::port
