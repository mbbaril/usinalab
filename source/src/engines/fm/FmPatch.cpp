// SPDX-License-Identifier: GPL-3.0-only
#include "FmPatch.h"
#include "FmSchema.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

namespace usina::fm {

bool Patch::isKnownName(const std::string& name) { return schemaIndex(name) >= 0; }

// the 256 values in the schema's order -> the patch; nothing allocated (the audio thread calls it on a change)
Patch Patch::fromArray(const int* v)
{
    Patch p;
    const auto& s = schema();
    const auto get = [&](int i) { return std::clamp(v[i], s[(size_t)i].min, s[(size_t)i].max); };
    const int velCoarse = get(globalIndex(GV_VDEST));
    for (int k = 0; k < 6; ++k) {
        OpParams& q = p.op[(size_t)k];
        const auto o = [&](int ov) { return get(opIndex(k, ov)); };
        q.mode = o(OV_MODE);
        q.wave = o(OV_WAVE);
        q.fixed = o(OV_FREQ) != 0;
        q.coarse = o(OV_COARSE);
        q.fine = o(OV_FINE);
        q.detune = o(OV_DETUNE);
        q.trans = o(OV_TRANS);
        q.fixedHz100 = o(OV_FIXHZ);
        q.level = o(OV_LEVEL);
        q.atk = o(OV_ATK);
        q.dec = o(OV_DEC);
        q.sus = o(OV_SUS);
        q.rel = o(OV_REL);
        q.curve = o(OV_CURVE);
        q.a = o(OV_A);
        q.b = o(OV_B);
        q.c = o(OV_C);
        q.d = o(OV_D);
        q.vel = o(OV_VEL);
        q.eg1 = o(OV_EG1);
        q.lfo1Pitch = o(OV_L1P);
        q.lfo1Level = o(OV_L1L);
        q.ktLow = o(OV_KTLO);
        q.ktHigh = o(OV_KTHI);
        q.ktCenter = o(OV_KTC);
        q.ktLowCurve = o(OV_KTLC);
        q.ktHighCurve = o(OV_KTHC);
        q.velPitch = get(globalIndex(GV_VP1 + k));
        q.velCoarse = ((velCoarse >> k) & 1) != 0;
    }
    p.alg = get(globalIndex(GV_ALG));
    p.phase = get(globalIndex(GV_PHASE));
    p.algFb = get(globalIndex(GV_ALGFB));
    p.directOut = get(globalIndex(GV_UDOUT)) & 63;
    for (int a = 0; a < 6; ++a)
        for (int b = 0; b < 6; ++b) p.mx[(size_t)a][(size_t)b] = get(globalIndex(GV_UMX + a * 6 + b));
    const auto g = [&](int gv) { return get(globalIndex(gv)); };
    VoiceFilterParams& f = p.flt;
    f.on = g(GV_FON) != 0;
    f.type = g(GV_FTYPE);
    f.cutoff = g(GV_FCUT);
    f.reso = g(GV_FRES);
    f.eg2Int = g(GV_FEG2);
    f.lfo2Int = g(GV_FLFO2);
    f.keyLow = g(GV_FKTLO);
    f.keyHigh = g(GV_FKTHI);
    f.keyCenter = g(GV_FKTC);
    f.hpCut = g(GV_HPCUT);
    f.hpPeak = g(GV_HPPEAK);
    f.drive = g(GV_FDRIVE);
    f.cascFb = g(GV_CFB);
    f.hpEg2Int = g(GV_HPEG2);
    f.hpLfo2Int = g(GV_HPLFO2);
    f.cascVca = g(GV_PVCA);
    f.cascOut = g(GV_COUT);
    f.cascHot = g(GV_CHOT) != 0;
    f.cmosLife = g(GV_CLIFE);
    f.eg2 = { g(GV_E2ATK), g(GV_E2DEC), g(GV_E2SUS), g(GV_E2REL), g(GV_E2CRV) };
    p.outGain = g(GV_OGAIN);
    p.lofi = g(GV_LOFI) != 0;
    p.eg1 = { g(GV_E1ATK), g(GV_E1DEC), g(GV_E1SUS), g(GV_E1REL), g(GV_E1CRV) };
    for (int k = 0; k < 2; ++k) {
        const int b = k ? GV_L2WAVE : GV_L1WAVE;
        p.lfo[k] = { g(b), g(b + 1), g(b + 2), g(b + 3), g(b + 4) };
    }
    const auto x = [&](int xv) { return get(extIndex(xv)); };
    p.lfo[2] = { x(XV_L3WAVE), x(XV_L3SPD), x(XV_L3KS), x(XV_L3PH), x(XV_L3FADE) };
    p.env1 = { x(XV_ENV1ATK), x(XV_ENV1DEC), x(XV_ENV1SUS), x(XV_ENV1REL), x(XV_ENV1CRV) };
    for (int k = 0; k < 3; ++k) {
        const int f = XV_F1MODE + k * kFuncValues, r = XV_R1TYPE + k * kRandValues, c = XV_C1TYPE + k * kCombValues;
        p.func[k] = { x(f), x(f + 1), x(f + 2), x(f + 3), x(f + 4) };
        p.rand[k] = { x(r), x(r + 1), x(r + 2), x(r + 3), x(r + 4), x(r + 5) };
        p.comb[k] = { x(c), x(c + 1), x(c + 2), x(c + 3) };
        p.func[k].sync = x(XV_F1SYNC + 2 * k);
        p.func[k].retrig = x(XV_F1SYNC + 2 * k + 1);
        p.rand[k].sync = x(XV_F1SYNC + 6 + 2 * k);
        p.rand[k].retrig = x(XV_F1SYNC + 6 + 2 * k + 1);
        auto& l = p.lfo[k];                              // (M4)
        const int lb = XV_L1SYNC + k * kLfoXValues;
        l.sync = x(lb);
        l.symmetry = x(lb + 1);
        l.waveB = x(lb + 2);
        l.morph = x(lb + 3);
        l.polarity = x(lb + 4);
        l.retrig = x(lb + 5);
    }
    p.lpgMode = std::clamp(x(XV_LPGMODE), 0, 2);       // the voice's LPG (21/10)
    p.lpgDecay = std::clamp(x(XV_LPGDECAY), 0, 100);
    p.lpgColor = std::clamp(x(XV_LPGCOLOR), 0, 100);
    for (int k = 0; k < 6; ++k) p.op[(size_t)k].rateKey = std::clamp(x(XV_O1RATEKEY + k), 0, 100);
    for (int k = 0; k < 6; ++k) p.op[(size_t)k].damp = std::clamp(x(XV_O1DAMP + k), 0, 100);
    for (int k = 0; k < 6; ++k) {
        p.op[(size_t)k].sub = std::clamp(x(XV_O1SUB + k), 0, 100);
        p.op[(size_t)k].pwm = std::clamp(x(XV_O1PWM + k), 0, 100);
    }
    for (int k = 0; k < 6; ++k) {                        // the operators' MODAL values
        auto& o = p.op[(size_t)k];
        const auto m = [&](int x) { return get(modalIndex(k, x)); };
        o.material = m(0);
        o.bright = m(1);
        o.stiff = m(2);
        o.position = m(3);
        o.density = m(4);
        o.irreg = m(5);
        o.strike = m(6);
        o.hardness = m(7);
        o.exciter = m(8);
        o.pressure = m(9);
        o.tension = m(10);
        o.rattle = m(11);
    }
    for (int e = 0; e < kNumEnvs; ++e) {                 // the envelopes' M4 values
        const int eb = XV_EV1MODE + e * kEnvXValues;
        const int md = x(eb), ac = x(eb + 1), rc = x(eb + 2), rt = x(eb + 3);
        if (e < 6) {
            auto& o = p.op[(size_t)e];
            o.envMode = md; o.atkCurve = ac; o.relCurve = rc; o.envRetrig = rt;
        } else {
            Adsr& a = e == EV_EG1 ? p.eg1 : e == EV_EG2 ? p.flt.eg2 : p.env1;
            a.mode = md; a.atkCurve = ac; a.relCurve = rc; a.retrig = rt;
        }
    }
    return p;
}

Patch Patch::fromArrayWithMacros(const int* values, const int macro[3])
{
    enum { AT_SLOT, AT_OFF, AT_MRAT, AT_FB, AT_DTUN, AT_PARAM };   // (eng_opx.c OPX_AT_*)
    const auto& s = schema();
    std::array<int, kNumValues> v;
    for (int i = 0; i < kNumValues; ++i) v[(size_t)i] = values[i];
    int grp[3] = { 0, 0, 0 };                            // the modulators' COARSE steps, FB %, DTUN 0..127
    for (int k = 0; k < 3; ++k) {
        int tg = v[(size_t)globalIndex(GV_A1T + 2 * k)], d = v[(size_t)globalIndex(GV_A1D + 2 * k)];
        const int x = std::clamp(macro[k], -64, 63);
        if (tg == AT_SLOT && !d) {                       // zeroed: the slot's classic macro
            tg = AT_MRAT + k;
            d = 100;
        }
        if (tg == AT_OFF || !d || !x) continue;
        if (tg == AT_MRAT) grp[0] += x * d * 16 / 6400;
        else if (tg == AT_FB) grp[1] += x * d / 64;
        else if (tg == AT_DTUN) grp[2] += std::abs(x) * 2 * d / 100;
        else if (tg >= AT_PARAM && tg - AT_PARAM < kFwValues) {   // (the firmware's values: its A TARGET ids)
            const int id = tg - AT_PARAM;
            const auto& dsc = s[(size_t)id];
            const int off = (int)((int64_t)x * d * (dsc.max - dsc.min) / 12800);
            v[(size_t)id] = std::clamp(v[(size_t)id] + off, dsc.min, dsc.max);
        }
    }
    Patch p = fromArray(v.data());
    p.mratSteps = std::clamp(grp[0], -16, 16);
    p.fbOffset = std::clamp(grp[1], -100, 100);
    p.dtunSpread = std::clamp(grp[2], 0, 127);
    return p;
}

// the preset language's named values -> the 256 (a name a preset leaves out: the schema's default)
std::array<int, kNumValues> Patch::valuesFrom(const std::map<std::string, int>& v, std::vector<std::string>* unknown)
{
    std::array<int, kNumValues> a {};
    const auto& s = schema();
    for (int i = 0; i < kNumValues; ++i) a[(size_t)i] = s[(size_t)i].def;
    for (const auto& [name, val] : v) {
        const int i = schemaIndex(name);
        if (i >= 0) a[(size_t)i] = val;
        else if (unknown) unknown->push_back(name);      // (never silently: docs/IA_PATCHES.md)
    }
    return a;
}

Patch Patch::fromValues(const std::map<std::string, int>& v, std::vector<std::string>* unknown)
{
    const auto a = valuesFrom(v, unknown);
    return fromArray(a.data());
}

} // namespace usina::fm
