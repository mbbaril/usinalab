// SPDX-License-Identifier: GPL-3.0-only
// Usina FM engine - the patch's values: every name of the CALDERA preset language, its range and its value in the
// init patch, in the firmware's order (usina16 eng_opx.c OPX_PD: the 27 values of OP1..OP6, then the 94 of the
// patch). One table for the patch reader (FmPatch), the plugin's parameters (their IDs come from these names and
// never change: rule 19) and the editor.
#pragma once
#include <string>
#include <vector>

namespace usina::fm {

// an operator's values, in order (index = k * kOpValues + this)
enum OpValue { OV_MODE, OV_WAVE, OV_FREQ, OV_COARSE, OV_FINE, OV_DETUNE, OV_TRANS, OV_FIXHZ, OV_LEVEL, OV_ATK, OV_DEC,
               OV_SUS, OV_REL, OV_CURVE, OV_A, OV_B, OV_C, OV_VEL, OV_EG1, OV_D, OV_L1P, OV_L1L, OV_KTLO, OV_KTHI,
               OV_KTC, OV_KTLC, OV_KTHC, kOpValues };
// the patch's values (index = 6 * kOpValues + this)
enum GlobalValue { GV_ALG, GV_PHASE, GV_E1ATK, GV_E1DEC, GV_E1SUS, GV_E1REL, GV_E1CRV, GV_FTYPE, GV_FON, GV_FCUT,
                   GV_FRES, GV_FEG2, GV_FLFO2, GV_FKTLO, GV_FKTHI, GV_FKTC, GV_E2ATK, GV_E2DEC, GV_E2SUS, GV_E2REL,
                   GV_E2CRV, GV_L1WAVE, GV_L1SPD, GV_L1KS, GV_L1PH, GV_L1FADE, GV_L2WAVE, GV_L2SPD, GV_L2KS, GV_L2PH,
                   GV_L2FADE, GV_A1T, GV_A1D, GV_A2T, GV_A2D, GV_A3T, GV_A3D, GV_ALGFB, GV_UDOUT,
                   GV_UMX, GV_HPCUT = GV_UMX + 36, GV_HPPEAK, GV_FDRIVE, GV_CFB, GV_HPEG2, GV_HPLFO2, GV_LOFI, GV_PVCA,
                   GV_COUT, GV_VP1, GV_VDEST = GV_VP1 + 6, GV_CHOT, GV_CLIFE, GV_OGAIN, kGlobalValues };
constexpr int kFwValues = 6 * kOpValues + kGlobalValues;      // 256: the firmware's (the CALDERA preset language)
// the Usina's own values, after the firmware's (a CALDERA preset leaves them at their defaults): ENV 1 (the
// modulation's own envelope; ENV 2 and 3 are EG1 and EG2), LFO 3 (as LFO1 and LFO2)
// FUNC k (k = 0..2): MODE, SPEED, POLARITY, SMOOTH, SUSTAIN (the point it holds at); RAND k: TYPE, SPEED, A, B,
// RISE, FALL; COMB k: TYPE, SOURCE, MOD, AMOUNT (each block of the three after the other: XV_F1MODE + 5 k ...)
enum ExtValue { XV_ENV1ATK, XV_ENV1DEC, XV_ENV1SUS, XV_ENV1REL, XV_ENV1CRV,
                XV_L3WAVE, XV_L3SPD, XV_L3KS, XV_L3PH, XV_L3FADE,
                XV_F1MODE, XV_F1SPD, XV_F1POL, XV_F1SMOOTH, XV_F1SUS,
                XV_R1TYPE = XV_F1MODE + 15, XV_R1SPD, XV_R1A, XV_R1B, XV_R1RISE, XV_R1FALL,
                XV_C1TYPE = XV_R1TYPE + 18, XV_C1SRC, XV_C1MOD, XV_C1AMT,
                // LFO k (M4): SYNC (0 free, else a division of the host's tempo), SYMMETRY, WAVE B, MORPH (to WAVE B),
                // POLARITY (as the wave, +, +/-), RETRIG (a source: its rise past 0.5 restarts the LFO)
                XV_L1SYNC = XV_C1TYPE + 12, XV_L1SYM, XV_L1WAVEB, XV_L1MORPH, XV_L1POL, XV_L1RETRIG,
                // envelope e (M4; EnvId order): MODE (ADSR, ADR), ATK CURVE, REL CURVE (-1: CURVE's), RETRIG
                XV_EV1MODE = XV_L1SYNC + 18, XV_EV1ACRV, XV_EV1RCRV, XV_EV1RETRIG,
                // operator k's MODAL values (FmModal.h; its A..D: STRUCTURE, OSC MIX, SIZE, DECAY): MATERIAL, BRIGHT,
                // STIFF, POSITION, DENSITY, IRREG, STRIKE, HARDNESS; stage 2: EXCITER, PRESSURE, TENSION, RATTLE
                XV_O1MAT = XV_EV1MODE + 36, XV_O1BRIGHT, XV_O1STIFF, XV_O1POS, XV_O1DENS, XV_O1IRREG, XV_O1STRIKE,
                XV_O1HARD, XV_O1EXCITER, XV_O1PRESSURE, XV_O1TENSION, XV_O1RATTLE,
                // FUNC k's and RAND k's SYNC (0 free, else kSyncBeats) and RETRIG (a source): FUNC 1..3, then RAND 1..3
                XV_F1SYNC = XV_O1MAT + 72, XV_F1RETRIG,
                // the voice's LPG (FmLpg.h, 21/10): MODE (OFF, PING, EG2), DECAY, COLOR
                XV_LPGMODE = XV_F1SYNC + 12, XV_LPGDECAY, XV_LPGCOLOR,
                // operator k's RATE KEY (22/10, the chips' key scale rate): 0..100 %, its envelope's times by the note
                XV_O1RATEKEY = XV_LPGMODE + 3,
                kExtValues = XV_O1RATEKEY + 6 };
constexpr int kFuncValues = 5, kRandValues = 6, kCombValues = 4;   // (one FUNC's, RAND's, COMB's values)
constexpr int kLfoXValues = 6, kEnvXValues = 4;                      // (one LFO's, one envelope's M4 values)
constexpr int kModalXValues = 12;                                    // (one operator's MODAL values)
constexpr int modalIndex(int op, int x) { return kFwValues + XV_O1MAT + op * kModalXValues + x; }   // x 0 = MATERIAL
// the envelopes with M4's values: the six operators', EG1, EG2, ENV 1
enum EnvId { EV_OP1, EV_EG1 = 6, EV_EG2, EV_ENV1, kNumEnvs };
// the divisions of SYNC (item 1..14), in beats (quarter notes)
constexpr double kSyncBeats[15] = { 0.0, 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5,
                                    2.0, 4.0, 8.0, 16.0 };
constexpr int kNumValues = kFwValues + kExtValues;
constexpr int opIndex(int op, int v) { return op * kOpValues + v; }   // op 0 = OP1
constexpr int globalIndex(int g) { return 6 * kOpValues + g; }
constexpr int extIndex(int x) { return kFwValues + x; }

struct ValueDesc {
    std::string name;                            // the preset language's ("OP1.LEVEL", "MX 1>3", "CUTOFF")
    std::string id;                              // the plugin parameter's ID ("op1_level", "mx_1to3", "cutoff")
    int min, max, def;
};

// every value, in order: the firmware's 256, then the Usina's own
const std::vector<ValueDesc>& schema();
// the index of a name, or -1; of a parameter ID ("op1_level"), or -1
int schemaIndex(const std::string& name);
int schemaIndexOfId(const std::string& id);

} // namespace usina::fm
