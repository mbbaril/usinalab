// SPDX-License-Identifier: GPL-3.0-only
// Usina FM engine - the MACRO operator's models, by name (FmMacro.h; light, for the editor)
#pragma once

namespace usina::fm {

constexpr int kMacroModels = 24;
inline const char* macroModelName(int m)
{
    static const char* const N[kMacroModels] = { "VA FILTER", "PHASE DIST", "6-OP A", "6-OP B", "6-OP C", "TERRAIN",
                                                 "STRINGS", "CHIP", "VIRTUAL AN", "WAVESHAPE", "2-OP FM", "GRAIN",
                                                 "ADDITIVE", "WAVETABLE", "CHORD", "SPEECH", "SWARM", "NOISE",
                                                 "PARTICLE", "STRING", "MODAL", "BASS DRUM", "SNARE", "HI-HAT" };
    return N[((m % kMacroModels) + kMacroModels) % kMacroModels];
}

} // namespace usina::fm
