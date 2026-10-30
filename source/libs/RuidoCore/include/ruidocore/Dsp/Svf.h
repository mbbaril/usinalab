#pragma once

#include <algorithm>
#include <cmath>

namespace ruidocore {

// See THIRD_PARTY.md and the preserved license notice for source provenance.
//
// Unlike a naive/non-ZDF SVF, this stays STABLE even at high resonance
// and high cutoff-relative-to-sample-rate, which matters a lot for a
// filter driven by a MOD generator sweeping cutoff quickly.
class Svf {
public:
    enum class Mode { LowPass, BandPass, BandPassNormalized, HighPass, Notch, Peak };

    void reset() { state1 = state2 = 0.0f; }

    // frequency: 0..0.5 (fraction of sample rate, i.e. already
    // frequencyHz / sampleRate - the caller decides how to derive this).
    // resonance: > 0, roughly "Q" - higher rings longer/peaks harder.
    void setFrequencyAndResonance(float frequency, float resonance) {
        frequency = std::min(frequency, 0.497f); // stays comfortably below Nyquist where tan() blows up
        g = std::tan(kPi * frequency);
        r = 1.0f / std::max(0.01f, resonance);
        h = 1.0f / (1.0f + r * g + g * g);
    }

    float process(float in, Mode mode) {
        const float hp = (in - r * state1 - g * state1 - state2) * h;
        const float bp = g * hp + state1;
        state1 = g * hp + bp;
        const float lp = g * bp + state2;
        state2 = g * bp + lp;

        switch (mode) {
            case Mode::LowPass: return lp;
            case Mode::BandPass: return bp;
            case Mode::BandPassNormalized: return bp * r;
            case Mode::HighPass: return hp;
            case Mode::Notch: return hp + lp; // rejects the band around the cutoff
            case Mode::Peak: return lp - hp;  // emphasizes the band around the cutoff
        }
        return lp;
    }

    // Simultaneous band-pass + low-pass read from ONE state update -
    // matches reference `Svf::Process<BAND_PASS,
    // LOW_PASS>(in, &bp_out, &lp_out)`, which several real engines (e.g.
    // AnalogBassDrum) rely on to get two DIFFERENT signals from the same
    // filter pass in one sample .
    void processBandPassAndLowPass(float in, float& bpOut, float& lpOut) {
        const float hp = (in - r * state1 - g * state1 - state2) * h;
        const float bp = g * hp + state1;
        state1 = g * hp + bp;
        const float lp = g * bp + state2;
        state2 = g * bp + lp;
        bpOut = bp;
        lpOut = lp;
    }

    // Multimode low-pass-to-high-pass crossfade used by filtered-noise synthesis.
    // Its transfer law differs from processMultimode; do not substitute the two.
    float processMultimodeLpToHp(float in, float mode) {
        const float hp = (in - r * state1 - g * state1 - state2) * h;
        const float bp = g * hp + state1;
        state1 = g * hp + bp;
        const float lp = g * bp + state2;
        state2 = g * bp + lp;
        const float hpGain = std::min(-mode * 2.0f + 1.0f, 0.0f);
        const float bpGain = 1.0f - 2.0f * std::abs(mode - 0.5f);
        const float lpGain = std::max(1.0f - mode * 2.0f, 0.0f);
        return hpGain * hp + bpGain * bp + lpGain * lp;
    }

    // Continuously-morphable "multimode" sweep: 0 = LowPass, 0.5 =
    // BandPass, 1.0 = HighPass, smoothly crossfaded - useful as a single
    // MOD-generator-driven "filter type" knob instead of a discrete
    // Mode switch.
    float processMultimode(float in, float mode01) {
        const float hp = (in - r * state1 - g * state1 - state2) * h;
        const float bp = g * hp + state1;
        state1 = g * hp + bp;
        const float lp = g * bp + state2;
        state2 = g * bp + lp;

        mode01 = std::clamp(mode01, 0.0f, 1.0f);
        const float lpGain = mode01 < 0.5f ? 1.0f - mode01 * 2.0f : 0.0f;
        const float bpGain = mode01 < 0.5f ? mode01 * 2.0f : 2.0f - mode01 * 2.0f;
        const float hpGain = mode01 < 0.5f ? 0.0f : mode01 * 2.0f - 1.0f;
        return lpGain * lp + bpGain * bp + hpGain * hp;
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;
    float g = 0.0f, r = 1.0f, h = 1.0f;
    float state1 = 0.0f, state2 = 0.0f;
};

} //
