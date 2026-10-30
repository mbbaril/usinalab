// SPDX-License-Identifier: GPL-3.0-only
// Stereo filter effect for the FX slots, built on the voice filter kernels (engines/fm/FmVoiceFilter.h).
//
// Parameters: type (voice filter types, SK CASC excluded), cutoff 20 Hz..20 kHz, resonance 0..1, drive x1..x8
// (saturating types only), spread +-1 octave per side, follow up to +-4 octaves (input level on a dB scale from
// -48 dBFS to 0 dBFS), mix, output gain.
// Input is scaled so that 0 dBFS matches an operator at full level inside a voice. Parameters glide over 20 ms;
// switching on or off fades over 20 ms. No latency, no allocation in process().
#pragma once
#include "engines/fm/FmVoiceFilter.h"

namespace usina::fx {

class Filter {
public:
    static constexpr int kNumTypes = usina::fm::kNumVoiceFilters - 1;

    struct Settings {
        int type = 0;              // 0 .. kNumTypes - 1
        float cutoffHz = 2000.0f;  // 20 .. 20000
        float reso = 0.0f;         // 0 .. 1
        float drive = 0.0f;        // 0 .. 1 -> x1 .. x8
        float spread = 0.0f;       // -1 .. 1; positive raises the right cutoff
        float follow = 0.0f;       // -1 .. 1
        float mix = 1.0f;          // 0 .. 1
        float outputDb = 0.0f;     // -24 .. 12
    };

    void prepare(double sampleRate);
    void reset();
    // In place. When off, the input passes unchanged once the fade-out completes.
    void process(float* l, float* r, int n, const Settings& s, bool on);
    bool active() const { return running; }

    // Maps an effect type index to the voice filter index, skipping SK CASC.
    static int voiceType(int type) { return type >= usina::fm::kCascType ? type + 1 : type; }
    static const char* typeName(int type);

private:
    usina::fm::VoiceFilter fl, fr;
    double fs = 48000.0;
    bool running = false, fresh = true;
    float fade = 0.0f, fadeStep = 0.001f;
    double lg = 0.0, spread = 0.0, mix = 1.0, out = 1.0, reso = 0.0, drive = 1.0, follow = 0.0;
    double glide = 0.001, env = 0.0, envUp = 0.0, envDown = 0.0;
};

} // namespace usina::fx
