// ---------------------------------------------------------------------------
// This file ports code from Emilie Gillet's stmlib (https://github.com/pichenettes/stmlib), used under the MIT
// licence. The upstream files it derives from: stmlib/dsp/cosine_oscillator.h (Copyright 2014), stmlib/dsp/
// parameter_interpolator.h (Copyright 2015), the parts of stmlib/dsp/dsp.h and stmlib/stmlib.h the reverb uses
// (Copyright 2012). Read at stmlib d18def8 (D:/upstream/stmlib), ported line for line.
//
//   Copyright 2012, 2014, 2015 Emilie Gillet.
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
// What changed from upstream, and only this: the namespace (usina::port); the macros DISALLOW_COPY_AND_ASSIGN and
// STATIC_ASSERT as C++'s own (= delete, static_assert); SLOPE as a function; Clip16 the portable (TEST) one, not the
// ARM ssat. The arithmetic is upstream's, operation for operation (tests/PortTest checks it bit for bit).
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

#ifndef M_PI                                            // (MSVC defines it only on request: the same value)
#define M_PI 3.14159265358979323846
#endif

namespace usina::port {

// stmlib/dsp/dsp.h
#define USINA_MAKE_INTEGRAL_FRACTIONAL(x)                   \
    int32_t x##_integral = static_cast<int32_t>(x);         \
    float x##_fractional = x - static_cast<float>(x##_integral);

inline void slope(float& out, float in, float positive, float negative)   // (SLOPE)
{
    float error = (in) - out;
    out += (error > 0 ? positive : negative) * error;
}

inline int32_t clip16(int32_t x)
{
    if (x < -32768) {
        return -32768;
    } else if (x > 32767) {
        return 32767;
    } else {
        return x;
    }
}

// stmlib/dsp/cosine_oscillator.h
enum CosineOscillatorMode { COSINE_OSCILLATOR_APPROXIMATE, COSINE_OSCILLATOR_EXACT };

class CosineOscillator {
public:
    CosineOscillator() {}
    CosineOscillator(const CosineOscillator&) = delete;
    void operator=(const CosineOscillator&) = delete;

    template <CosineOscillatorMode mode>
    inline void Init(float frequency)
    {
        if (mode == COSINE_OSCILLATOR_APPROXIMATE) {
            InitApproximate(frequency);
        } else {
            iir_coefficient_ = 2.0f * cosf(2.0f * float(M_PI) * frequency);
            initial_amplitude_ = iir_coefficient_ * 0.25f;
        }
        Start();
    }

    inline void InitApproximate(float frequency)
    {
        float sign = 16.0f;
        frequency -= 0.25f;
        if (frequency < 0.0f) {
            frequency = -frequency;
        } else {
            if (frequency > 0.5f) {
                frequency -= 0.5f;
            } else {
                sign = -16.0f;
            }
        }
        iir_coefficient_ = sign * frequency * (1.0f - 2.0f * frequency);
        initial_amplitude_ = iir_coefficient_ * 0.25f;
    }

    inline void Start()
    {
        y1_ = initial_amplitude_;
        y0_ = 0.5f;
    }

    inline float value() const { return y1_ + 0.5f; }

    inline float Next()
    {
        float temp = y0_;
        y0_ = iir_coefficient_ * y0_ - y1_;
        y1_ = temp;
        return temp + 0.5f;
    }

private:
    float y1_ = 0.0f;
    float y0_ = 0.0f;
    float iir_coefficient_ = 0.0f;
    float initial_amplitude_ = 0.0f;
};

// stmlib/dsp/parameter_interpolator.h
class ParameterInterpolator {
public:
    ParameterInterpolator() {}
    ParameterInterpolator(float* state, float new_value, size_t size) { Init(state, new_value, size); }

    ParameterInterpolator(float* state, float new_value, float step)
    {
        state_ = state;
        value_ = *state;
        increment_ = (new_value - *state) * step;
    }

    ~ParameterInterpolator() { *state_ = value_; }

    inline void Init(float* state, float new_value, size_t size)
    {
        state_ = state;
        value_ = *state;
        increment_ = (new_value - *state) / static_cast<float>(size);
    }

    inline float Next()
    {
        value_ += increment_;
        return value_;
    }

    inline float subsample(float t) { return value_ + increment_ * t; }

private:
    float* state_ = nullptr;
    float value_ = 0.0f;
    float increment_ = 0.0f;
};

} // namespace usina::port
