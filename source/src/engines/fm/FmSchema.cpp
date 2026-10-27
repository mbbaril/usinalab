// SPDX-License-Identifier: GPL-3.0-only
#include "FmSchema.h"
#include <cstdlib>
#include <map>

namespace usina::fm {

namespace {
struct D { const char* name; int min, max, def; };

// an operator's 27 (eng_opx.c OPX_PD). FIXED HZ from 0: a preset stores 0 for "never set" (the operator then is not
// fixed; the firmware's SET writes >= 1). The defaults: the init patch's operator (OP1 differs: LEVEL 100)
const D kOp[kOpValues] = {
    { "MODE", 0, 11, 0 }, { "WAVE", 0, 32, 0 }, { "FREQ", 0, 1, 0 }, { "COARSE", 0, 38, 7 }, { "FINE", 50, 200, 100 },
    { "DETUNE", -50, 50, 0 }, { "TRANS", -12, 12, 0 }, { "FIXED HZ", 0, 999999, 0 }, { "LEVEL", 0, 100, 0 },
    { "ATK", 0, 127, 0 }, { "DEC", 0, 127, 0 }, { "SUS", 0, 100, 100 }, { "REL", 0, 127, 26 }, { "CURVE", 0, 10, 0 },
    { "A", 0, 100, 0 }, { "B", 0, 100, 0 }, { "C", 0, 200, 100 }, { "VEL", 0, 100, 0 }, { "EG1", -2304, 2304, 0 },
    { "D", 0, 100, 0 }, { "LFO1 PITCH", -2304, 2304, 0 }, { "LFO1 LEVEL", -100, 100, 0 }, { "KT LOW", -100, 100, 0 },
    { "KT HIGH", -100, 100, 0 }, { "KT CENTER", 21, 120, 60 }, { "KT L CURVE", 0, 1, 0 }, { "KT H CURVE", 0, 1, 0 } };

// the patch's 94, the matrix (36) at GV_UMX, written below
constexpr int kTargets = 5 + 6 * kOpValues + kGlobalValues - 1;   // A1..A3 TARGET: OPX_AT_PARAM + every value
const D kGlobal[] = {
    { "ALGO", 0, 40, 0 }, { "PHASE", 0, 2, 0 }, { "EG1 ATK", 0, 127, 0 }, { "EG1 DEC", 0, 127, 0 },
    { "EG1 SUS", 0, 100, 0 }, { "EG1 REL", 0, 127, 0 }, { "EG1 CURVE", 0, 10, 0 }, { "FILTER TYPE", 0, 17, 0 },
    { "FILTER", 0, 1, 0 }, { "CUTOFF", 0, 1000, 1000 }, { "RESO", 0, 100, 0 }, { "EG2 INT", -2272, 2272, 0 },
    { "LFO2 INT", -2272, 2272, 0 }, { "KEY LOW", -200, 200, 0 }, { "KEY HIGH", -200, 200, 0 },
    { "KEY CENTER", 0, 127, 60 }, { "EG2 ATK", 0, 127, 0 }, { "EG2 DEC", 0, 127, 0 }, { "EG2 SUS", 0, 100, 100 },
    { "EG2 REL", 0, 127, 26 }, { "EG2 CURVE", 0, 10, 0 }, { "LFO1 WAVE", 0, 25, 0 }, { "LFO1 SPEED", 0, 127, 64 },
    { "LFO1 KEY SYNC", 0, 2, 0 }, { "LFO1 PHASE", -180, 181, 0 }, { "LFO1 FADE", 0, 127, 0 },
    { "LFO2 WAVE", 0, 25, 0 }, { "LFO2 SPEED", 0, 127, 64 }, { "LFO2 KEY SYNC", 0, 2, 0 },
    { "LFO2 PHASE", -180, 181, 0 }, { "LFO2 FADE", 0, 127, 0 }, { "A1 TARGET", 0, kTargets, 0 },
    { "A1 DEPTH", -100, 100, 0 }, { "A2 TARGET", 0, kTargets, 0 }, { "A2 DEPTH", -100, 100, 0 },
    { "A3 TARGET", 0, kTargets, 0 }, { "A3 DEPTH", -100, 100, 0 }, { "ALG FB", 0, 100, 0 },
    { "DIRECT OUT", 0, 63, 1 } };
const D kGlobalTail[] = {
    { "HP CUT", 0, 1000, 0 }, { "HP PEAK", 0, 100, 0 }, { "FLT DRIVE", 0, 100, 0 }, { "CASC FB", -100, 100, 0 },
    { "HP EG2 INT", -2272, 2272, 0 }, { "HP LFO2 INT", -2272, 2272, 0 }, { "LOFI", 0, 1, 0 }, { "CASC VCA", 0, 2, 0 },
    { "CASC OUT", 0, 32, 0 }, { "OP1 VEL PITCH", -100, 100, 0 }, { "OP2 VEL PITCH", -100, 100, 0 },
    { "OP3 VEL PITCH", -100, 100, 0 }, { "OP4 VEL PITCH", -100, 100, 0 }, { "OP5 VEL PITCH", -100, 100, 0 },
    { "OP6 VEL PITCH", -100, 100, 0 }, { "VEL COARSE", 0, 63, 0 }, { "CASC HOT", 0, 1, 0 }, { "CMOS LIFE", 0, 100, 0 },
    { "OUT GAIN", 0, 32, 0 } };
const D kExt[] = {
    { "ENV1 ATK", 0, 127, 0 }, { "ENV1 DEC", 0, 127, 64 }, { "ENV1 SUS", 0, 100, 0 }, { "ENV1 REL", 0, 127, 64 },
    { "ENV1 CURVE", 0, 10, 10 }, { "LFO3 WAVE", 0, 25, 4 }, { "LFO3 SPEED", 0, 127, 64 }, { "LFO3 KEY SYNC", 0, 2, 0 },
    { "LFO3 PHASE", -180, 181, 0 }, { "LFO3 FADE", 0, 127, 0 } };
static_assert(sizeof(kExt) / sizeof(kExt[0]) == XV_F1MODE, "ENV 1 and LFO 3, before FUNC, RAND, COMB");
static_assert(sizeof(kGlobal) / sizeof(kGlobal[0]) == GV_UMX, "the patch's values before the matrix");
static_assert(sizeof(kGlobalTail) / sizeof(kGlobalTail[0]) == kGlobalValues - GV_HPCUT, "the values after it");

// a name -> its parameter ID: lower case, "." and " " as "_", ">" as "to" ("OP1.LEVEL" -> "op1_level")
std::string idOf(const std::string& name)
{
    std::string s;
    for (char c : name) {
        if (c == '.' || c == ' ') s += '_';
        else if (c == '>') s += "to";
        else s += (char)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    }
    return s;
}

std::vector<ValueDesc> build()
{
    std::vector<ValueDesc> v;
    v.reserve(kNumValues);
    const auto add = [&](const std::string& n, int mn, int mx, int df) { v.push_back({ n, idOf(n), mn, mx, df }); };
    for (int k = 0; k < 6; ++k)
        for (const D& d : kOp)
            add("OP" + std::to_string(k + 1) + "." + d.name, d.min, d.max,
                k == 0 && std::string(d.name) == "LEVEL" ? 100 : d.def);
    for (const D& d : kGlobal) add(d.name, d.min, d.max, d.def);
    for (int a = 1; a <= 6; ++a)
        for (int b = 1; b <= 6; ++b) add("MX " + std::to_string(a) + ">" + std::to_string(b), 0, 100, 0);
    for (const D& d : kGlobalTail) add(d.name, d.min, d.max, d.def);
    for (const D& d : kExt) add(d.name, d.min, d.max, d.def);
    for (int k = 1; k <= 3; ++k) {                      // FUNC 1..3 (their points: the session's, FmModSources.h)
        const std::string f = "FUNC" + std::to_string(k) + " ";
        add(f + "MODE", 0, 3, 1);                        // LOOP, ONE-SHOT, ENVELOPE, ENVELOPE LOOP
        add(f + "SPEED", 0, 127, 64);                    // (as the LFOs': 0.01 .. 100 Hz, the whole curve once)
        add(f + "POLARITY", 0, 1, 0);                    // 0..1, or -1..1
        add(f + "SMOOTH", 0, 100, 0);                    // ms
        add(f + "SUSTAIN", 0, 63, 1);                    // the point ENVELOPE holds at (up to kMaxFuncPoints)
    }
    for (int k = 1; k <= 3; ++k) {                      // RAND 1..3
        const std::string r = "RAND" + std::to_string(k) + " ";
        add(r + "TYPE", 0, 2, 0);                        // RANDOM LOOP, SAMPLE & HOLD, BINARY
        add(r + "SPEED", 0, 127, 80);
        add(r + "A", 0, 100, 25);                        // FLIP; (S&H: none); PROBA
        add(r + "B", 0, 100, 50);                        // LENGTH; (S&H: none); CORREL
        add(r + "RISE", 0, 127, 0);                      // the rise and the fall smoothed (time indices)
        add(r + "FALL", 0, 127, 0);
    }
    for (int k = 1; k <= 3; ++k) {                      // COMB 1..3
        const std::string c = "COMB" + std::to_string(k) + " ";
        add(c + "TYPE", 0, 12, 0);                       // (FmModSources.h CombType)
        add(c + "SOURCE", 0, 24, 0);                     // a source (FmMod.h; a COMB only the ones before it)
        add(c + "MOD", 0, 24, 0);
        add(c + "AMOUNT", -100, 100, 50);
    }
    for (int k = 1; k <= 3; ++k) {                      // LFO 1..3, M4 (their defaults: the LFO as before)
        const std::string l = "LFO" + std::to_string(k) + " ";
        add(l + "SYNC", 0, 14, 0);                       // FREE, 1/32 .. 4 BARS (kSyncBeats)
        add(l + "SYMMETRY", -100, 100, 0);               // the first half of the cycle longer (+) or shorter
        add(l + "WAVE B", 0, 25, 4);                     // MORPH's other wave
        add(l + "MORPH", 0, 100, 0);
        add(l + "POLARITY", 0, 2, 0);                    // AS WAVE, + (0..1), +/- (-1..1)
        add(l + "RETRIG", 0, 24, 0);                     // a source (FmMod.h), or none
    }
    for (int e = 0; e < kNumEnvs; ++e) {                // the envelopes, M4 (the defaults: as the firmware's)
        const std::string p = e < 6 ? "OP" + std::to_string(e + 1) + ".ENV " : e == EV_EG1 ? "EG1 " : e == EV_EG2 ? "EG2 "
                                                                                                          : "ENV1 ";
        add(p + "MODE", 0, 2, 0);                        // ADSR, ADR (the key's length ignored: no sustain), LOOP
                                                         // (attack and decay again while the key is held, 22/10)
        add(p + "ATK CURVE", 0, 10, 0);                  // linear (the firmware's) .. fast-then-slow
        add(p + "REL CURVE", -1, 10, -1);                // -1: CURVE's (as the firmware: one curve for both)
        add(p + "RETRIG", 0, 24, 0);                     // a source: its rise past 0.5 strikes the envelope again
    }
    for (int k = 1; k <= 6; ++k) {                      // the operators' MODAL values (FmModal.h, docs/FISICA.md)
        const std::string o = "OP" + std::to_string(k) + ".";
        add(o + "MATERIAL", 0, 16, 0);                   // STEEL .. CARDBOARD
        add(o + "BRIGHT", -100, 100, 0);                 // the highs dying faster (-) or slower (+) than the material
        add(o + "STIFF", -100, 100, 0);                  // the partials squeezed (-) or stretched (+)
        add(o + "POSITION", 0, 100, 30);                 // where it is struck
        add(o + "DENSITY", 1, 64, 32);                   // modes
        add(o + "IRREG", 0, 100, 0);                     // added to the material's own irregularity
        add(o + "STRIKE", 0, 100, 80);                   // the mallet at every note
        add(o + "HARDNESS", 0, 100, 60);                 // the mallet's (and the input's) brightness
        add(o + "EXCITER", 0, 3, 0);                     // stage 2: STRIKE, BOW, BLOW, SCRAPE
        add(o + "PRESSURE", 0, 100, 50);                 // the bow's pressure, the breath's turbulence, the scrape's rate
        add(o + "TENSION", 0, 100, 0);                   // the pitch rising with the amplitude (a gong, a tom)
        add(o + "RATTLE", 0, 100, 0);                    // a contact buzzing on the peaks (a snare, a chain, a bridge)
    }
    for (int k = 0; k < 6; ++k) {                       // FUNC 1..3, RAND 1..3: SYNC and RETRIG (as the LFOs')
        const std::string s = (k < 3 ? "FUNC" : "RAND") + std::to_string(k % 3 + 1) + " ";
        add(s + "SYNC", 0, 14, 0);                       // FREE, 1/32 .. 4 BARS (kSyncBeats): FUNC its curve once a
        add(s + "RETRIG", 0, 24, 0);                     // division, RAND a step; a source's rise restarts it
    }
    add("LPG MODE", 0, 2, 0);                            // the voice's low pass gate (FmLpg.h): OFF, PING, EG2
    add("LPG DECAY", 0, 100, 50);
    add("LPG COLOR", 0, 100, 50);
    for (int k = 1; k <= 6; ++k)                        // the operators' RATE KEY: at 100 %, an octave above KT CENTER
        add("OP" + std::to_string(k) + ".RATE KEY", 0, 100, 0);   // halves the envelope's times, one below doubles
    // (every value: the firmware's, then ours, exactly as the enums count them)
    if ((int)v.size() != kNumValues) std::abort();       // (a table that disagrees with the enums: stop at once)
    return v;
}
} // namespace

const std::vector<ValueDesc>& schema()
{
    static const std::vector<ValueDesc> s = build();
    return s;
}

int schemaIndex(const std::string& name)
{
    static const std::map<std::string, int> m = [] {
        std::map<std::string, int> r;
        const auto& s = schema();
        for (int i = 0; i < (int)s.size(); ++i) r[s[(size_t)i].name] = i;
        return r;
    }();
    const auto it = m.find(name);
    return it == m.end() ? -1 : it->second;
}

int schemaIndexOfId(const std::string& id)
{
    static const std::map<std::string, int> m = [] {
        std::map<std::string, int> r;
        const auto& s = schema();
        for (int i = 0; i < (int)s.size(); ++i) r[s[(size_t)i].id] = i;
        return r;
    }();
    const auto it = m.find(id);
    return it == m.end() ? -1 : it->second;
}

} // namespace usina::fm
