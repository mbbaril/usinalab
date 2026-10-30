#pragma once

#include "Overdrive.h"

#include <cmath>

namespace ruidocore {

// Selectable distortion character . `Drive` reuses
// the existing real-ported `Overdrive` unchanged. `Wavefolder` and
// `Bitcrush` are generic, well-known DSP techniques (not hardware
// clones needing a source to download - no fidelity-discipline gap
// here, unlike e.g. a Sallen-Key filter would need).
enum class DistortionMode { Drive, Wavefolder, Bitcrush };

// One stateful stage covering all three characters - the caller picks
// which `process()` overload behavior applies via `mode`, and only the
// relevant internal state (Overdrive's pre/post gain, or Bitcrush's
// sample-and-hold) is actually touched per call.
struct DistortionStage {
    Overdrive overdrive;

    void setAmount(DistortionMode mode, float amount01) {
        storedAmount = amount01;
        if (mode == DistortionMode::Drive)
            overdrive.setDrive(amount01);
    }

    float process(DistortionMode mode, float in) {
        switch (mode) {
            case DistortionMode::Drive: return overdrive.process(in);
            case DistortionMode::Wavefolder: return wavefold(in);
            case DistortionMode::Bitcrush: return bitcrush(in);
        }
        return in;
    }

private:
    // Triangle-fold: push the signal past +-1 proportionally to
    // `amount`, then reflect it back into range - same structural idea
    // as ModEngine's own `applyModWavefold` (see Mod/ModEngine.cpp),
    // translated from that function's 0..1 unipolar LFO-shaping domain
    // into this one's bipolar -1..1 audio domain.
    float wavefold(float in) {
        if (storedAmount <= 0.0f)
            return in;
        const float drive = 1.0f + storedAmount * 8.0f;
        float v = in * drive;
        while (v > 1.0f || v < -1.0f) {
            if (v > 1.0f)
                v = 2.0f - v;
            else if (v < -1.0f)
                v = -2.0f - v;
        }
        return v;
    }

    // Classic lo-fi crush: reduces both bit depth (amplitude
    // quantization) and effective sample rate (a sample-and-hold that
    // grows longer as `amount` increases) - two independent, additive
    // sources of "digital grit," not just one or the other.
    float bitcrush(float in) {
        if (storedAmount <= 0.0f)
            return in;
        if (bitcrushHoldCounter <= 0) {
            const float bits = 16.0f - storedAmount * 13.0f; // 16 (clean) down to 3 (heavily crushed)
            const float halfLevels = std::exp2(bits) * 0.5f;
            bitcrushHoldSample = std::round(in * halfLevels) / halfLevels;
            bitcrushHoldCounter = 1 + static_cast<int>(storedAmount * storedAmount * 24.0f); // up to a 25-sample hold at max amount
        }
        --bitcrushHoldCounter;
        return bitcrushHoldSample;
    }

    float storedAmount = 0.0f;
    float bitcrushHoldSample = 0.0f;
    int bitcrushHoldCounter = 0;
};

} //
