// SPDX-License-Identifier: GPL-3.0-only
// Granular, delay and tape processor with an internal room stage.
// Read controls every 1/4000 s through smoothing, pitch quantization and attenuverters.
// Media rates: 48 kHz digital/clean tape, 32 kHz vintage, 24 kHz dirty tape.
// Rate conversion is bypassed when host and processor rates match.
// Media changes include 250 ms silence; bypass transitions fade over 20 ms.
// SEED accepts note gates or host beats. See THIRD_PARTY.md for DSP attribution.
//
// Latency includes rate conversion and one processing block; see latencySamples().
// Host latency is not reported. No allocation in process().
#pragma once
#include <memory>

namespace usina::fx {

class Texture {
public:
    enum Media { kDirtyTape = 0, kCleanTape, kVintage, kDigital, kNumMedia };   // The processor's own order
    enum SeedMode {
        kSeedInternal = 0,                               // The SEED button latched: DENSITY makes the grains
        kSeedNotes,                                      // The notes into the SEED input: grains while a note is held
        kSeedNotesDivided,                               // The notes as a clock, DENSITY dividing it
        kSeedHostClock,                                  // The host's beats as a clock (the delay in time with it)
        kNumSeedModes
    };

    // The panel, as knob positions 0..1 (the module's laws make them its values)
    struct Knobs {
        float time = 0.5f, size = 0.5f, shape = 0.5f, pitch = 0.5f;          // Pitch: 0.5 = unison
        float timeAmt = 0.5f, sizeAmt = 0.5f, shapeAmt = 0.5f, pitchAmt = 0.5f;   // Attenuverters (0.5 = none)
        float density = 0.5f, feedback = 0.0f, dryWet = 0.5f, reverb = 0.0f;
        float gain = 0.0f;                               // Input gain: 0 = automatic, else manual (x1..x40)
        bool freeze = false, mono = false;
        int media = kDigital, seedMode = kSeedInternal;
    };

    Texture();
    ~Texture();

    void prepare(double hostRate, int maxBlock);         // Allocates: never on the audio thread
    void reset();                                        // Reset to silence. Process stereo audio in place; gate supplies host-rate SEED values
    // or nullptr. Bypass fades over 20 ms, stops processing and clears state.
    void process(float* l, float* r, int n, const Knobs& k, const bool* gate, bool on);
    bool active() const;
    int latencySamples() const;                          // Latency in host samples at the current media rate.
    // Process bus dry audio through matched converters with no intervening DSP.
    // Returns false without modifying audio when a pure delay suffices.
    // Call once per block before process() while the bus is active.
    bool dryTwin(float* l, float* r, int n);

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};

} // Namespace usina::fx
