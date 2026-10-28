// SPDX-License-Identifier: GPL-3.0-only
// Usina FM engine - a patch, read from the CALDERA preset language: the same value names as the firmware
// ("OP1.LEVEL", "ALGO", "MX 1>3" ...; usina16 eng_opx.c OPX_PD), so the library's presets load as they are.
// The names, ranges and order: FmSchema.h. Read today: what the operators, their envelopes, the algorithms and the
// matrix need; the voice filter, the LFOs, EG1/EG2 and the macros come with their stages (docs/ARQUITETURA.md).
#pragma once
#include "FmSchema.h"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace usina::fm {

// an envelope: time indices 0..127 (FmTables envMs), sustain %, the decay / release curve 0 (linear) .. 10 (exp);
// M4: MODE 0 ADSR / 1 ADR, the attack's curve 0 (linear) .. 10, the release's own (-1: curve's), RETRIG a source
struct Adsr { int atk = 0, dec = 0, sus = 100, rel = 26, curve = 0, mode = 0, atkCurve = 0, relCurve = -1, retrig = 0; };

// the voice filter (after the operators) and SK CASC (the part's), as the firmware's opx_patch_t flt
struct VoiceFilterParams {
    bool on = false;
    int type = 0, cutoff = 1000, reso = 0;       // 0..17 (FmFilters voiceFilterType); 0..1000 (6.489 Hz .. 23.68 kHz)
    int eg2Int = 0, lfo2Int = 0;                 // EG2, LFO2 -> cutoff, 1/16 semitone (+-2272: +-142 semitones)
    int keyLow = 0, keyHigh = 0, keyCenter = 60; // key track below / above the centre, % (100: as the key)
    int hpCut = 0, hpPeak = 0;                   // SK CASC: the high-pass (0..1000, its peak 0..100)
    int drive = 0;                               // the saturating types' input drive 0..100 (x1 .. x8)
    int cascFb = 0;                              // SK CASC: its output back in, -100..100 (x -2 .. 2)
    int hpEg2Int = 0, hpLfo2Int = 0;             // SK CASC: the high-pass's own EG2, LFO2 depths
    int cascVca = 0, cascOut = 0;                // SK CASC: the part's VCA (0 GATE, 1 EG2, 2 ENV); its level (+0.75 dB)
    bool cascHot = false;                        // SK CASC: both filters at the signal's level
    int cmosLife = 0;                            // CMOS: its instability 0..100
    Adsr eg2;                                    // EG2: the filter's envelope
};

struct OpParams {
    int mode = 0, wave = 0;
    bool fixed = false;
    int coarse = 7, fine = 100, detune = 0, trans = 0;
    int fixedHz100 = 0;                          // FIXED HZ in 1/100 Hz
    int level = 0;                               // 0..100, 0.75 dB a step
    int atk = 0, dec = 0, sus = 100, rel = 26, curve = 0;   // envelope time indices 0..127, sustain %
    int a = 0, b = 0, c = 100, d = 0;            // the mode's four values
    int vel = 0;                                 // velocity -> level 0..100 (up to -30 dB)
    int eg1 = 0, lfo1Pitch = 0, lfo1Level = 0;   // (stage 4)
    int ktLow = 0, ktHigh = 0, ktCenter = 60, ktLowCurve = 0, ktHighCurve = 0;
    int velPitch = 0;                            // OPn VEL PITCH -100..100
    bool velCoarse = false;                      // VEL COARSE bit: VEL PITCH moves COARSE (else FINE)
    int envMode = 0, atkCurve = 0, relCurve = -1, envRetrig = 0;   // (M4: the envelope's, Adsr)
    // MODAL (its A..D: STRUCTURE, OSC MIX, SIZE, DECAY): MATERIAL, BRIGHT -100..100, STIFF -100..100, POSITION,
    // DENSITY 1..64, IRREG, STRIKE, HARDNESS (0..100)
    int material = 0, bright = 0, stiff = 0, position = 30, density = 32, irreg = 0, strike = 80, hardness = 60;
    int exciter = 0, pressure = 50, tension = 0, rattle = 0;   // (stage 2: STRIKE / BOW / BLOW / SCRAPE; 0..100)
    int rateKey = 0;                             // RATE KEY 0..100: the envelope's times by the note (KT CENTER's)
    int damp = 0;                                // MODAL DAMP 0..100: a hand on the object at the key's release
    int sub = 0, pwm = 0;                        // UNISON SUB, PWM 0..100 (FmUnison.h)
    Adsr adsr() const { return { atk, dec, sus, rel, curve, envMode, atkCurve, relCurve, envRetrig }; }
};

struct Patch {
    std::array<OpParams, 6> op {};               // OP1 first
    int alg = 0;                                 // 0..39 the preset algorithms, 40 = the user matrix
    int lpgMode = 0, lpgDecay = 50, lpgColor = 50;   // the voice's LPG (FmLpg.h): OFF, PING, EG2; 0..100, 0..100
    int phase = 0;                               // 0 every operator from 0, 1 one random phase, 2 each its own
    int algFb = 0;                               // ALG FB 0..100 (algorithms 4, 6, 34)
    int directOut = 1;                           // the matrix: bit k = OP(k+1) is heard
    std::array<std::array<int, 6>, 6> mx {};     // the matrix [from][to] 0..100, OP1 first ([k][k]: SELF)
    VoiceFilterParams flt;
    int outGain = 0;                             // OUT GAIN 0..32: the level after the filter, +0.75 dB a step
    bool lofi = false;                           // LOFI: the levels in 0.75 dB steps, the envelopes in 64, 12 bits
    Adsr eg1;                                    // EG1: the operators' pitch envelope (each its depth: OpParams eg1)
    // LFO1 (the operators' pitch and level), LFO2 (the filter): a wave 0..22, SPEED 0..127 (0.01 .. 100 Hz), KEY
    // SYNC 0 OFF / 1 COMMON / 2 VOICE, the PHASE a key restarts it at (degrees; 181 = random), a FADE in (a time index)
    // (lfo[2]: LFO 3, the Usina's own: a modulation source only)
    // M4: SYNC (0 free, else kSyncBeats), SYMMETRY -100..100, WAVE B and MORPH 0..100 (to it), POLARITY 0 as the
    // wave / 1 + / 2 +-, RETRIG a source
    struct Lfo {
        int wave = 0, speed = 64, keySync = 0, phase = 0, fade = 0;
        int sync = 0, symmetry = 0, waveB = 4, morph = 0, polarity = 0, retrig = 0;
    } lfo[3];
    Adsr env1;                                   // ENV 1: the modulation's own envelope (ENV 2, 3 = EG1, EG2)
    // the modulation's own sources' settings (FmModSources.h; FUNC's points are the session's, not the patch's)
    // (sync: 0 free, else kSyncBeats; retrig: a source whose rise restarts it)
    struct Func { int mode = 1, speed = 64, polarity = 0, smooth = 0, sustain = 1, sync = 0, retrig = 0; } func[3];
    struct Rand { int type = 0, speed = 80, a = 25, b = 50, rise = 0, fall = 0, sync = 0, retrig = 0; } rand[3];
    struct Comb { int type = 0, src = 0, mod = 0, amount = 50; } comb[3];
    // what A1..A3 (the macros) make of the classic targets (FmPatch applyMacros): the modulators' COARSE steps, every
    // FM operator's FB %, the carriers' detune spread (0..127)
    int mratSteps = 0, fbOffset = 0, dtunSpread = 0;

    // from the preset language; unknown names are listed (never silently ignored: docs/IA_PATCHES.md)
    static Patch fromValues(const std::map<std::string, int>& values, std::vector<std::string>* unknown = nullptr);
    // the same as the schema's 256 values (FmSchema.h), in order; allocates nothing (the audio thread's path)
    static Patch fromArray(const int* values);
    // the patch as A1..A3 move it (eng_opx.c opx_sync): macro[k] -64..63 (0: as it is) times A(k+1) DEPTH on A(k+1)
    // TARGET - a value of the patch (by its range), or a classic one (the modulators' ratio, FB, the carriers'
    // detune spread; TARGET 0 with DEPTH 0: the slot's own classic one at 100 %). Allocates nothing
    static Patch fromArrayWithMacros(const int* values, const int macro[3]);
    // named values -> the 256 (the ones left out: the schema's defaults)
    static std::array<int, kNumValues> valuesFrom(const std::map<std::string, int>& values,
                                           std::vector<std::string>* unknown = nullptr);
    // every value name of the firmware's patch (FmSchema.h; the ones a later stage reads are accepted already)
    static bool isKnownName(const std::string& name);
};

} // namespace usina::fm
