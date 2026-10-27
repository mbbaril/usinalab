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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ruidocore {

// A faithful port of Mutable Instruments' Plaits `DelayLine`
// (plaits/dsp/physical_modelling/delay_line.h, MIT licensed,
// github.com/pichenettes/eurorack, downloaded and read directly - "nao
// simplifique nada"/"quero fidelidade total", 2026-08-27). Backs
// Dsp/PlaitsString.h's Karplus-Strong waveguide. A ring buffer with a
// DECREASING write pointer (so `Read`'s modular arithmetic reads
// "backwards in time" cleanly) plus three read strategies: linear
// (`Read`), 4th-order Hermite (`ReadHermite`, needs the sample before
// and two after - same technique this library's own WavetableEngine/
// AnalogBassDrum already use elsewhere), and a one-tap allpass
// (`Allpass`, used for the dispersion nonlinearity).
template <typename T, size_t kMaxDelay>
class DelayLine {
public:
    void reset() {
        line.fill(T(0));
        writePtr = 0;
    }

    void write(T sample) {
        line[writePtr] = sample;
        writePtr = (writePtr - 1 + kMaxDelay) % kMaxDelay;
    }

    T allpass(T sample, size_t delay, T coefficient) {
        T read = line[(writePtr + delay) % kMaxDelay];
        T w = sample + coefficient * read;
        write(w);
        return -w * coefficient + read;
    }

    T read(float delay) const {
        const auto delayIntegral = static_cast<std::int32_t>(delay);
        const float delayFractional = delay - static_cast<float>(delayIntegral);
        const T a = line[(writePtr + static_cast<size_t>(delayIntegral)) % kMaxDelay];
        const T b = line[(writePtr + static_cast<size_t>(delayIntegral) + 1) % kMaxDelay];
        return a + (b - a) * T(delayFractional);
    }

    T readHermite(float delay) const {
        const auto delayIntegral = static_cast<std::int32_t>(delay);
        const float delayFractional = delay - static_cast<float>(delayIntegral);
        const auto t = static_cast<std::int64_t>(writePtr) + delayIntegral + static_cast<std::int64_t>(kMaxDelay);
        const T xm1 = line[static_cast<size_t>((t - 1 + static_cast<std::int64_t>(kMaxDelay)) % static_cast<std::int64_t>(kMaxDelay))];
        const T x0 = line[static_cast<size_t>(t % static_cast<std::int64_t>(kMaxDelay))];
        const T x1 = line[static_cast<size_t>((t + 1) % static_cast<std::int64_t>(kMaxDelay))];
        const T x2 = line[static_cast<size_t>((t + 2) % static_cast<std::int64_t>(kMaxDelay))];
        const T c = (x1 - xm1) * T(0.5);
        const T v = x0 - x1;
        const T w = c + v;
        const T a = w + v + (x2 - x0) * T(0.5);
        const T bNeg = w + a;
        const T f = T(delayFractional);
        return (((a * f) - bNeg) * f + c) * f + x0;
    }

private:
    std::array<T, kMaxDelay> line{};
    size_t writePtr = 0;
};

} // namespace ruidocore
