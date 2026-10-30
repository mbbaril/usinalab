// SPDX-License-Identifier: GPL-3.0-only
// Six-mode granular processor running at 32 kHz in 32-sample blocks.
// Rate conversion surrounds the processor unless the host also runs at 32 kHz.
// Input is quantized to 16 bits; controls use block smoothing and pitch quantization.
// Background splice and spectral work runs between audio blocks.
// Bypass, mode and quality transitions use fades. See THIRD_PARTY.md for attribution.
//
// Clamp controls to 65535/65536: exactly 1 would index beyond lookup tables.
//
// LIMIT bounds internal feedback power relative to held input power, with 6 dB headroom.
// The held reference decays slowly to preserve tails. Limiting acts inside the block;
// controlling the external feedback knob cannot constrain the buffered loop immediately.
// Below threshold the signal is unchanged. FREEZE bypasses this limit.
//
// Latency includes converters and one block; see latencySamples().
// Latency is not reported to the host. No allocation in process().
#pragma once
#include <memory>

namespace usina::fx {

class Vapor {
public:
    enum Mode { kGranular = 0, kStretch, kLoopingDelay, kSpectral, kReverb, kResonator, kNumModes };   //
    enum Quality { kStereo16 = 0, kMono16, kStereo8, kMono8, kNumQualities };   // 8-bit quality modes run at half rate. Controls are normalized to 0..1.
    struct Knobs {
        float position = 0.5f, size = 0.5f, pitch = 0.5f, density = 0.5f, texture = 0.5f;   // Pitch: 0.5 = unison
        float dryWet = 0.5f, spread = 0.0f, feedback = 0.0f, reverb = 0.0f;
        bool freeze = false, reverse = false;
        bool limit = false;                              // Optional internal feedback limiter; disabled by default.
        int mode = kGranular, quality = kStereo16;
    };

    Vapor();
    ~Vapor();

    void prepare(double hostRate, int maxBlock);         // Allocates: never on the audio thread
    void reset();                                        // Reset to silence. Process audio in place with host-rate TRIG gates or nullptr.
    // Bypass fades over 20 ms, stops processing and clears state.
    void process(float* l, float* r, int n, const Knobs& k, const bool* gate, bool on);
    bool active() const;
    int latencySamples() const;                          // Host samples

    static const char* modeName(int m);
    static const char* qualityName(int q);
    // What each knob does in a mode (the module's own mapping: granular_processor.cc), for the panel's captions
    enum Knob { kPosition = 0, kSize, kPitch, kDensity, kTexture, kDryWet, kSpread, kFeedback, kReverbKnob, kNumKnobs };
    static const char* knobName(int mode, int knob);
    static float pitchSemitones(float knob);             // PITCH's law (the module's quantized table), for its text

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};

} // Namespace usina::fx
