// SPDX-License-Identifier: GPL-3.0-only
// Usina FM engine - the modulation: 24 sources and the routes from them to the patch's values (docs/MODULACAO.md).
//   the sources   keyboard (velocity, aftertouch, mod wheel, key, expression), ENV 1..3 (ENV 2 = EG1, ENV 3 = EG2),
//                 LFO 1..3, FUNC 1..3, RAND 1..3, COMB 1..3 (their stage: 0 until then), MACRO 1..4; then SEQ 1..4,
//                 CTRL 1..8, and the operators' own envelopes OP1 ENV .. OP6 ENV (21/10; new ones only at the end)
//   a route       a source, a destination (a value of the patch, by its index in FmSchema), an amount -1..1: the
//                 share of the value's range the source moves it at full (the source at 1: + amount x the range)
// The engine reads the routes once a control block per voice and moves the destinations continuously (between the
// values' integer steps): the pitch, the levels, the modes' A..D, the filter, the output. Plain C++, no JUCE.
#pragma once
#include "FmSchema.h"
#include <array>
#include <string>

namespace usina::fm {

enum ModSource {
    MS_NONE,
    MS_VEL, MS_AT, MS_MW, MS_KEY, MS_EXPR,
    MS_ENV1, MS_ENV2, MS_ENV3,
    MS_LFO1, MS_LFO2, MS_LFO3,
    MS_FUNC1, MS_FUNC2, MS_FUNC3,
    MS_RAND1, MS_RAND2, MS_RAND3,
    MS_COMB1, MS_COMB2, MS_COMB3,
    MS_MAC1, MS_MAC2, MS_MAC3, MS_MAC4,
    MS_SEQ1, MS_SEQ2, MS_SEQ3, MS_SEQ4,                  // (the sequencer's modulation lanes, 0..1: src/seq)
    MS_CTRL1, MS_CTRL2, MS_CTRL3, MS_CTRL4, MS_CTRL5, MS_CTRL6, MS_CTRL7, MS_CTRL8,   // (the controllers: a CC or a note)
    MS_OPENV1, MS_OPENV2, MS_OPENV3, MS_OPENV4, MS_OPENV5, MS_OPENV6,   // (each operator's envelope, 0..1: the voice's)
    MS_TIMBRE,                                            // appended: stable IDs of all older sources are preserved
    kNumSources
};

// the families (the menu's columns, the colours): KEYBOARD, ENVELOPES, LFO, FUNCTIONS, RANDOM, COMBINATE, MACRO, SEQ,
// CONTROLLERS
enum ModFamily { MF_NONE, MF_KEYBOARD, MF_ENV, MF_LFO, MF_FUNC, MF_RAND, MF_COMB, MF_MACRO, MF_SEQ, MF_CTRL };
inline ModFamily modFamily(int s)
{
    if ((s >= MS_VEL && s <= MS_EXPR) || s == MS_TIMBRE) return MF_KEYBOARD;
    if ((s >= MS_ENV1 && s <= MS_ENV3) || (s >= MS_OPENV1 && s <= MS_OPENV6)) return MF_ENV;
    if (s >= MS_LFO1 && s <= MS_LFO3) return MF_LFO;
    if (s >= MS_FUNC1 && s <= MS_FUNC3) return MF_FUNC;
    if (s >= MS_RAND1 && s <= MS_RAND3) return MF_RAND;
    if (s >= MS_COMB1 && s <= MS_COMB3) return MF_COMB;
    if (s >= MS_MAC1 && s <= MS_MAC4) return MF_MACRO;
    if (s >= MS_SEQ1 && s <= MS_SEQ4) return MF_SEQ;
    if (s >= MS_CTRL1 && s <= MS_CTRL8) return MF_CTRL;
    return MF_NONE;
}
inline const char* modSourceName(int s)
{
    static const char* const N[kNumSources] = { "NONE", "VELOCITY", "AFTERTOUCH", "MOD WHEEL", "KEYBOARD", "EXPRESSION",
                                                "ENV 1", "ENV 2 (EG1)", "ENV 3 (EG2)", "LFO 1", "LFO 2", "LFO 3",
                                                "FUNC 1", "FUNC 2", "FUNC 3", "RAND 1", "RAND 2", "RAND 3",
                                                "COMB 1", "COMB 2", "COMB 3", "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4",
                                                "SEQ 1", "SEQ 2", "SEQ 3", "SEQ 4", "CTRL 1", "CTRL 2", "CTRL 3",
                                                "CTRL 4", "CTRL 5", "CTRL 6", "CTRL 7", "CTRL 8", "OP1 ENV", "OP2 ENV",
                                                "OP3 ENV", "OP4 ENV", "OP5 ENV", "OP6 ENV", "TIMBRE (CC74)" };
    return s >= 0 && s < kNumSources ? N[s] : "NONE";
}
// a source's ID in a saved session (stable forever, as the parameters' IDs: rule 19)
inline const char* modSourceId(int s)
{
    static const char* const I[kNumSources] = { "none", "vel", "at", "mw", "key", "expr", "env1", "env2", "env3",
                                                "lfo1", "lfo2", "lfo3", "func1", "func2", "func3", "rand1", "rand2",
                                                "rand3", "comb1", "comb2", "comb3", "mac1", "mac2", "mac3", "mac4",
                                                "seq1", "seq2", "seq3", "seq4", "ctrl1", "ctrl2", "ctrl3", "ctrl4",
                                                "ctrl5", "ctrl6", "ctrl7", "ctrl8", "openv1", "openv2", "openv3",
                                                "openv4", "openv5", "openv6", "timbre" };
    return s >= 0 && s < kNumSources ? I[s] : "none";
}
// a source swings both ways (-1..1: the key, the LFOs, the randoms, the combinations, the macros) or one (0..1)
inline bool modSourceBipolar(int s)
{
    const ModFamily f = modFamily(s);
    return s == MS_KEY || f == MF_LFO || f == MF_RAND || f == MF_COMB || f == MF_MACRO;
}
// a source that can be picked (every one, now that FUNC, RAND and COMB are built)
inline bool modSourceReady(int s) { return s > MS_NONE && s < kNumSources; }

struct Route {
    int src = MS_NONE;
    int dst = -1;                                // a value's index (FmSchema)
    float amount = 0.0f;                         // -1..1 of the value's range
};
constexpr int kMaxRoutes = 64;
struct Routes {
    std::array<Route, kMaxRoutes> r {};
    int n = 0;
};

// the values a route can move (continuously, a voice's own): the operators' pitch (FINE, DETUNE, TRANS), LEVEL,
// the modes' A, B, C, D, their EG1 and LFO1 depths; the voice filter's CUTOFF, RESO, DRIVE, EG2 and LFO2 depths,
// SK CASC's HP CUT, HP PEAK, CASC FB; OUT GAIN; MODAL's own continuous values; the envelopes' times, sustains and
// curves (read as each stage begins, a voice's own); the modulation sources' own settings. (The lists, the matrix
// and ALG FB, the operators' VEL and KT: not yet - docs/BACKLOG.md, "tudo o que faz sentido ser modulado")
inline bool isModulatable(int idx)
{
    if (idx < 0 || idx >= kNumValues) return false;
    if (idx < 6 * kOpValues) {
        const int v = idx % kOpValues;
        return v == OV_FINE || v == OV_DETUNE || v == OV_TRANS || v == OV_LEVEL || v == OV_A || v == OV_B ||
               v == OV_C || v == OV_D || v == OV_EG1 || v == OV_L1P || v == OV_L1L ||
               v == OV_ATK || v == OV_DEC || v == OV_SUS || v == OV_REL || v == OV_CURVE;   // (its envelope)
    }
    if (idx < kFwValues) {
        const int g = idx - 6 * kOpValues;
        return g == GV_FCUT || g == GV_FRES || g == GV_FDRIVE || g == GV_FEG2 || g == GV_FLFO2 || g == GV_HPCUT ||
               g == GV_HPPEAK || g == GV_CFB || g == GV_OGAIN ||
               (g >= GV_E1ATK && g <= GV_E1CRV) || (g >= GV_E2ATK && g <= GV_E2CRV) ||   // EG1, EG2
               g == GV_L1SPD || g == GV_L1FADE || g == GV_L2SPD || g == GV_L2FADE;        // LFO1, LFO2
    }
    // the sources' own settings (the user, 21/10: "tempo de lfo1 por lfo2 e por ai vai"): ENV 1's, LFO 3's SPEED and
    // FADE, FUNC's SPEED and SMOOTH, RAND's SPEED, A, B, RISE, FALL, COMB's AMOUNT, the LFOs' SYMMETRY and MORPH, the
    // envelopes' ATK CURVE
    {
        const int x = idx - kFwValues;
        if (x >= XV_ENV1ATK && x <= XV_ENV1CRV) return true;
        if (x == XV_L3SPD || x == XV_L3FADE) return true;
        if (x >= XV_F1MODE && x < XV_R1TYPE) { const int f = (x - XV_F1MODE) % kFuncValues; return f == 1 || f == 3; }
        if (x >= XV_R1TYPE && x < XV_C1TYPE) return (x - XV_R1TYPE) % kRandValues != 0;
        if (x >= XV_C1TYPE && x < XV_L1SYNC) return (x - XV_C1TYPE) % kCombValues == 3;
        if (x >= XV_L1SYNC && x < XV_EV1MODE) { const int l = (x - XV_L1SYNC) % kLfoXValues; return l == 1 || l == 3; }
        if (x >= XV_EV1MODE && x < XV_O1MAT) return (x - XV_EV1MODE) % kEnvXValues == 1;
        if (x == XV_LPGDECAY || x == XV_LPGCOLOR) return true;   // (the LPG's: not its MODE, a list)
    }
    // MODAL's continuous values (FmModal.h): BRIGHT, STIFF, POSITION, IRREG, STRIKE, HARDNESS, PRESSURE, TENSION,
    // RATTLE (not MATERIAL nor EXCITER, lists; nor DENSITY, a count)
    if (idx >= modalIndex(0, 0) && idx < modalIndex(6, 0)) {
        const int x = (idx - modalIndex(0, 0)) % kModalXValues;
        return x == 1 || x == 2 || x == 3 || x == 5 || x == 6 || x == 7 || x == 9 || x == 10 || x == 11;
    }
    return false;
}

// The routes' other destinations: the effects' knobs - one for the plugin, not a voice's (21/10: the texture moved by
// the matrix). Their indices sit past the patch's (kFxDst + n), so the engine, which takes only isModulatable() ones,
// never sees them: the processor moves these, from the sources as the newest voice has them (Engine::liveSource), on
// the knob's own 0..1 travel (amount 1 = the whole travel). Saved by their names, as the patch's values. New ones only
// at the end (the distortion's, 21/10, after the texture's; VAPOR's, 22/10, after the IR's; DELAY's after VAPOR's)
constexpr int kFxDst = 1 << 20;
enum FxDst { FD_TEX_TIME, FD_TEX_SIZE, FD_TEX_SHAPE, FD_TEX_PITCH, FD_TEX_DENSITY, FD_TEX_FEEDBACK, FD_TEX_DRYWET,
             FD_TEX_REVERB, FD_DIST_DRIVE, FD_DIST_CHAR, FD_DIST_SYM, FD_DIST_TONE, FD_DIST_MIX,
             FD_IR_MIX, FD_IR_LOW, FD_IR_HIGH, FD_VAP_POSITION, FD_VAP_SIZE, FD_VAP_PITCH, FD_VAP_DENSITY,
             FD_VAP_TEXTURE, FD_VAP_DRYWET, FD_VAP_SPREAD, FD_VAP_FEEDBACK, FD_VAP_REVERB,
             FD_DLY_TIME, FD_DLY_FEEDBACK, FD_DLY_LOWCUT, FD_DLY_HIGHCUT, FD_DLY_WOW, FD_DLY_MIX, FD_DLY_FLUTTER,
             kNumFxDst };
inline bool isFxDst(int d) { return d >= kFxDst && d < kFxDst + kNumFxDst; }
inline const char* fxDstName(int d)                     // (stable forever: saved sessions name them)
{
    static const char* const N[kNumFxDst] = { "TEX TIME", "TEX SIZE", "TEX SHAPE", "TEX PITCH", "TEX DENSITY",
                                              "TEX FEEDBACK", "TEX DRY/WET", "TEX REVERB", "DIST DRIVE",
                                              "DIST CHAR", "DIST SYM", "DIST TONE", "DIST MIX", "IR MIX",
                                              "IR LOW CUT", "IR HIGH CUT", "VAP POSITION", "VAP SIZE", "VAP PITCH",
                                              "VAP DENSITY", "VAP TEXTURE", "VAP DRY/WET", "VAP SPREAD",
                                              "VAP FEEDBACK", "VAP REVERB", "DLY TIME", "DLY FEEDBACK",
                                              "DLY LOW CUT", "DLY HIGH CUT", "DLY WOW", "DLY MIX", "DLY FLUTTER" };
    return isFxDst(d) ? N[d - kFxDst] : "";
}
inline const char* fxDstParamId(int d)                  // (the plugin parameter the knob is)
{
    static const char* const I[kNumFxDst] = { "tex_time", "tex_size", "tex_shape", "tex_pitch", "tex_density",
                                              "tex_feedback", "tex_drywet", "tex_reverb", "dist_drive",
                                              "dist_char", "dist_sym", "dist_tone", "dist_mix", "ir_mix",
                                              "ir_lowcut", "ir_highcut", "vap_position", "vap_size", "vap_pitch",
                                              "vap_density", "vap_texture", "vap_drywet", "vap_spread",
                                              "vap_feedback", "vap_reverb", "dly_time", "dly_feedback",
                                              "dly_lowcut", "dly_highcut", "dly_wow", "dly_mix", "dly_flutter" };
    return isFxDst(d) ? I[d - kFxDst] : "";
}
inline int fxDstOfParamId(const std::string& id)
{
    for (int i = 0; i < kNumFxDst; ++i)
        if (id == fxDstParamId(kFxDst + i)) return kFxDst + i;
    return -1;
}
// any destination a route can have: a patch value that moves, or an effect's knob
inline bool isRouteTarget(int d) { return isModulatable(d) || isFxDst(d); }
inline std::string routeTargetName(int d)
{
    if (isFxDst(d)) return fxDstName(d);
    return d >= 0 && d < kNumValues ? schema()[(size_t)d].name : std::string();
}
inline int routeTargetIndex(const std::string& name)
{
    for (int i = 0; i < kNumFxDst; ++i)
        if (name == fxDstName(kFxDst + i)) return kFxDst + i;
    return schemaIndex(name);
}

} // namespace usina::fm
