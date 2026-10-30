// SPDX-License-Identifier: GPL-3.0-only
// Minimum-phase FIR conversion by real cepstrum.
// Log magnitude -> cepstrum -> causal folding -> exponentiated spectrum.
// Double positive-time coefficients and discard negative-time coefficients.
// Compute outside the audio thread; FFT oversizing limits cepstral aliasing.
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace usina {

namespace minphase_detail {
// In place, n a power of two; inverse: the conjugate transform, scaled by 1 / n
inline void fft(std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {                // Bit reversal
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    constexpr double kPi = 3.14159265358979323846;
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * kPi / (double)len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= (double)n;
}
}

// H: a real filter (any phase); its minimum-phase twin, the same length; fftSize a power of two >= 8 x h's length
inline std::vector<double> minimumPhase(const std::vector<double>& h, size_t fftSize)
{
    using minphase_detail::fft;
    std::vector<std::complex<double>> a(fftSize);
    for (size_t i = 0; i < h.size() && i < fftSize; ++i) a[i] = h[i];
    fft(a, false);
    double peak = 0.0;
    for (const auto& x : a) peak = std::max(peak, std::abs(x));
    const double floor = peak * 1.0e-9;                  // -180 dB: below any stopband here, never log 0
    for (auto& x : a) x = std::log(std::max(std::abs(x), floor));
    fft(a, true);                                        // The real cepstrum
    const size_t half = fftSize / 2;
    for (size_t i = 1; i < half; ++i) a[i] *= 2.0;       // Folded: the causal part doubled, the rest dropped
    for (size_t i = half + 1; i < fftSize; ++i) a[i] = 0.0;
    fft(a, false);
    for (auto& x : a) x = std::exp(x);                   // The minimum-phase spectrum
    fft(a, true);
    std::vector<double> out(h.size());
    for (size_t i = 0; i < out.size(); ++i) out[i] = a[i].real();
    return out;
}

} // Namespace usina
