#pragma once

#include <algorithm>
#include <cmath>

namespace ruidocore {

// Zero-delay-feedback (topology-preserving transform) state-variable
// filter - one coefficient update per block/parameter-change, one
// Process() call per sample, LP/BP/HP all available simultaneously from
// the same two integrator states. This specific trapezoidal-integration
// SVF derivation (g/r/h coefficients, hp/bp/lp all solved from one
// algebraic system per sample) is the same well-known topology used in
// Mutable Instruments' `stmlib::Svf` (MIT-licensed,
// github.com/pichenettes/stmlib/blob/master/dsp/filter.h - read for this
// library's own synth-engine research, see docs/SYNTH_ENGINE_RESEARCH.md)
// and in countless other real synths - independently reimplemented here
// (not copied) in this library's own style, but crediting the same
// underlying derivation rather than pretending it was invented fresh.
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
    // matches real Plaits' own `stmlib::Svf::Process<BAND_PASS,
    // LOW_PASS>(in, &bp_out, &lp_out)`, which several real engines (e.g.
    // AnalogBassDrum) rely on to get two DIFFERENT signals from the same
    // filter pass in one sample (2026-08-27, "faça o mesmo método de
    // baixar o código" fidelity review - calling process() twice with
    // different Modes would silently double-update state1/state2 and
    // corrupt the filter, which is exactly the bug this method exists to
    // avoid).
    void processBandPassAndLowPass(float in, float& bpOut, float& lpOut) {
        const float hp = (in - r * state1 - g * state1 - state2) * h;
        const float bp = g * hp + state1;
        state1 = g * hp + bp;
        const float lp = g * bp + state2;
        state2 = g * bp + lp;
        bpOut = bp;
        lpOut = lp;
    }

    // Exact port of real Plaits' own `Svf::ProcessMultimodeLPtoHP` -
    // used by the Filtered Noise engine's own multimode crossfade knob.
    // NOT the same formula as this class's own `processMultimode` below
    // (that one is this library's own invented convenience utility) -
    // this one has a specific quirk real Plaits actually has: at the
    // full-HP end (mode=1.0) the HP gain goes NEGATIVE (-1.0, not
    // +1.0), an inverted-phase highpass, not a generic crossfade -
    // faithfully kept rather than "corrected."
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

} // namespace ruidocore
