// SPDX-License-Identifier: GPL-3.0-only
#include "FmEngine.h"
#include "FmMacro.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace usina::fm {

namespace {
constexpr double kPi = 3.14159265358979323846;
// a carrier at full level (2.0) reaches 2.0 x kOut. Calibrated by tools/ab against the firmware's emulator (its
// voice, mixer and master): 0.125 measured +11.77 dB above it on every preset (spread 0.08 dB), so the presets
// levelled on the firmware (-18 dBFS peak, -26 dBFS RMS) land at the same level here
constexpr double kOut = 0.125 * 0.25800;

// FB % -> the feedback gain, as the firmware's opx_fbk (Q16 there: 10925 = a saw's harmonics at 75 %), / 2^17 here
inline float fbGain(int pct)
{
    const int q = pct <= 75 ? pct * 10925 / 75 : 10925 + (pct - 75) * 437;
    return (float)q / 131072.0f;
}

// the folder sin(pi x / 2) and its antiderivative (opx_fold, opx_fold_F), antialiased by the first-order
// antiderivative method (Parker, Zavalishin, Le Bivic): the mean of the folder between the last input and this one
inline float fold(float x) { return (float)std::sin(kPi * 0.5 * x); }
inline float foldF(float x) { return (float)(-2.0 / kPi * std::cos(kPi * 0.5 * x)); }
inline float foldAA(float x, float& x1)
{
    const float d = x - x1;
    const float y = std::fabs(d) > 1.0f / 16.0f ? (foldF(x) - foldF(x1)) / d : fold(0.5f * (x + x1));
    x1 = x;
    return y;
}

// the level key track (opx_ktrk): a log2 offset; LIN 1 dB a semitone at 100 %, EXP 12 dB x (2^(octaves) - 1)
double keyTrack(const OpParams& o, int note)
{
    const int c = o.ktCenter ? o.ktCenter : 60, dist = note - c;
    const int s = dist < 0 ? o.ktLow : o.ktHigh, a = std::abs(dist);
    if (!s || !dist) return 0.0;
    const bool expo = dist < 0 ? o.ktLowCurve != 0 : o.ktHighCurve != 0;
    const double db = expo ? (std::exp2(std::min(a, 60) / 12.0) - 1.0) * 12.0 : (double)a;
    return db / 6.0206 * s / 100.0;
}

// the level's gain: log2 15 - (100 - LEVEL) / 8 (0.75 dB a step) + velocity (up to -30 dB) + key track, at most 17;
// gk = 2^(lg - 14) (2.0 at LEVEL 100). Made again every control block (the firmware's frk/gk): an edit of LEVEL,
// VEL or the key track is heard on the held notes
double levelGain(const OpParams& po, int vel, int note, bool lofi, double levelMod = 0.0)
{
    const double level = std::min(po.level + levelMod, 100.0);   // (a route moves LEVEL: 0 and under is silent)
    if (level <= 0.0) return 0.0;
    const double vlog = -(127 - vel) * po.vel * (5.0 / 12700.0) + keyTrack(po, note);
    double lg = std::min(15.0 - (100.0 - level) / 8.0 + vlog, 17.0);
    if (lofi) lg = std::floor(lg * 8.0) / 8.0;            // LOFI: the level in 0.75 dB steps
    return std::exp2(lg - 14.0);
}

// CUTOFF and HP CUT's 0..1000 span 6.489 Hz .. 23.68 kHz: this many octaves
double cutSpan()
{
    static const double s = std::log2(23680.0 / 6.489);
    return s;
}
} // namespace

uint32_t Engine::xorshift(uint32_t& s)
{
    uint32_t x = s ? s : 0x6D2B79F5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s = x;
}

void Engine::prepare(double rate, bool firmware2x, double band)
{
    fs = rate > 0.0 ? rate : 48000.0;
    hdScale = band > 0.0 ? fs / band : 1.0;
    fw2x = firmware2x;
    dcCoef = 1.0 - std::exp(-2.0 * kPi * 6.86 / fs);    // the firmware's voice DC blocker (1/1024 at 44.1 kHz)
    ctl = std::max(1, (int)std::lround(32.0 * fs / (750000.0 / 17.0)));
    fx.prepare(fs, !fw2x);                               // (the distortions antialiased, but in the firmware's way)
    for (auto& v : voices) {                             // (everything sized here, never on the audio thread)
        v = Voice {};
        for (auto& o : v.op) o.fx.line.assign((size_t)fx.lineLength(), 0.0f);
        v.vf.prepare(fs, fw2x);
        v.lpg.prepare(fs);
    }
    // the MACRO operators' models: made once (never on the audio thread), prepared for this rate
    if (macros.empty())
        for (int i = 0; i < kMaxVoices * 6; ++i) macros.emplace_back(newMacro());
    for (auto& m : macros) m->prepare(band > 0.0 ? band : fs, std::max(1, (int)std::lround(hdScale)));
    part = Part {};
    part.casc.prepare(fs, fw2x);
    for (int k = 0; k < kNumLfos; ++k) {
        lph[k] = 0.0;
        lr[k] = LfoRandom {};
    }
    for (int k = 0; k < 3; ++k) {                        // RAND 1..3: each its own sequence (one seed: three twins)
        rands[(size_t)k] = RandGen {};
        rands[(size_t)k].rng = 0x2545F491u + 0x9E3779B9u * (uint32_t)(k + 1);
        randVal[k] = 0.0;
    }
    engPos = 0;
    partBuf.assign((size_t)kChunk, 0.0f);
    fofsOk = false;
    derive();
}

void Engine::Slide::block(double target, double dyn, int n)
{
    if (fresh) {                                         // (a new voice starts where it is)
        s0 = s1 = target;
        pv = target + dyn;
        cur = pv;
        fresh = false;
    }
    s0 += (target - s0) / 16.0;
    s1 += (s0 - s1) / 16.0;
    const double lg = s1 + dyn;
    cur = pv;
    step = (lg - pv) / n;
    pv = lg;
}

// the filter's key track: (the note - KEY CENTER) x KEY LOW / HIGH % of a semitone a semitone, in octaves
double Engine::keyTrack(int note) const
{
    const int dist = note - patch.flt.keyCenter;
    return dist * (dist < 0 ? patch.flt.keyLow : patch.flt.keyHigh) / 1200.0;
}

void Engine::setPatch(const Patch& p)
{
    patch = p;
    derive();
}

// the patch -> the per-sample settings, in the core order (k = OP(6 - k)), as eng_opx.c opx_sync
void Engine::derive()
{
    const auto& T = Tables::get();
    d = Derived {};
    for (auto& row : d.mx) row.fill(0.0f);
    if (patch.alg >= kNumAlgs) {                     // USER: the patch's matrix (OP1 = the core's 5)
        for (int j = 0; j < 6; ++j) {
            for (int i = 0; i < 6; ++i) d.mx[(size_t)(5 - j)][(size_t)(5 - i)] = patch.mx[(size_t)j][(size_t)i] / 100.0f;
            if ((patch.directOut >> j) & 1) d.dout |= (uint8_t)(1u << (5 - j));
        }
        d.car = d.dout;
    } else {                                         // a preset algorithm as links (opx_alg_matrix)
        const auto& f = T.alg[(size_t)patch.alg];
        uint8_t bus[3] = { 0, 0, 0 };
        for (int k = 0; k < 6; ++k) {
            const int inb = (f[(size_t)k] >> 4) & 3, outb = f[(size_t)k] & 3;
            if (inb)
                for (int j = 0; j < 6; ++j)
                    if ((bus[inb] >> j) & 1) d.mx[(size_t)j][(size_t)k] = 1.0f;
            if (!outb) d.dout |= (uint8_t)(1u << k);
            else if (f[(size_t)k] & 4) bus[outb] |= (uint8_t)(1u << k);
            else bus[outb] = (uint8_t)(1u << k);
        }
        d.car = d.dout;
        if (patch.algFb && (patch.alg == 3 || patch.alg == 5 || patch.alg == 33))   // ALG FB: 4 > 6, 5 > 6, 4 > 6
            d.mx[patch.alg == 5 ? 1 : 2][0] = patch.algFb / 100.0f;
    }
    static const int kSpread[6] = { 0, 1, -1, 2, -2, 3 };   // the carriers' detune spread, OP1 first (opx_sync)
    int nCar = 0;
    for (int op = 0; op < 6; ++op) {                 // op: OP1 first; k: the core's index (OP(6 - k))
        const int k = 5 - op;
        const OpParams& o = patch.op[(size_t)op];
        const bool isCar = ((d.car >> k) & 1) != 0;
        d.mode[(size_t)k] = o.mode;
        d.wave[(size_t)k] = o.wave;
        d.fixed[(size_t)k] = o.fixed;
        // a macro's ratio steps move the modulators' COARSE; its detune spreads the carriers apart
        const int co = std::clamp(o.coarse + (isCar ? 0 : patch.mratSteps), 0, kNumCoarse - 1);
        const double dt = isCar ? kSpread[nCar++] * patch.dtunSpread * 12.0 / 127.0 / 1200.0 : 0.0;
        d.baseLog2[(size_t)k] = o.fixed ? std::log2(std::max(o.fixedHz100, 1) / 100.0)
                                        : T.coarseLog2[(size_t)co] + std::log2(o.fine / 100.0) + o.trans / 12.0 +
                                              o.detune / 1200.0 + dt;
        d.l1Level[(size_t)k] = std::clamp(o.lfo1Level, -100, 100) / 100.0;
        d.oc[(size_t)k] = opCoef(o, patch.fbOffset, 0.0, 0.0, 0.0, 0.0);
    }
    // the voice filter (eng_opx.c opx_sync): CUTOFF and HP CUT 0..1000 over 6.489 Hz .. 23.68 kHz, exponential;
    // FLT DRIVE 2^(3 d / 100); CASC FB x2 at 100; the gains +0.75 dB a step
    const auto& f = patch.flt;
    const double lo = std::log2(6.489), span = std::log2(23680.0 / 6.489);
    d.cascOn = f.on && f.type == kCascType;
    d.filterOn = f.on && !d.cascOn;
    d.flg = lo + std::clamp(f.cutoff, 0, 1000) * span / 1000.0;
    d.hlg = lo + std::clamp(f.hpCut, 0, 1000) * span / 1000.0;
    d.drive = std::exp2(3.0 * std::clamp(f.drive, 0, 100) / 100.0);
    d.cascFb = std::clamp(f.cascFb, -100, 100) * 2.0 / 100.0;
    d.outGain = std::pow(10.0, 0.75 * std::clamp(patch.outGain, 0, 32) / 20.0);
    d.cascGain = std::pow(10.0, 0.75 * std::min(32, std::clamp(f.cascOut, 0, 32) + std::clamp(patch.outGain, 0, 32)) / 20.0);
    // the LFOs: SPEED 0..127 = 0.01 .. 100 Hz (exponential), a control block's share of a cycle; the fade's step;
    // the phase a key restarts them at
    lfoRates();
    for (int k = 0; k < kNumLfos; ++k) {
        const auto& l = patch.lfo[k];
        const double fadeMs = T.envMs[(size_t)std::clamp(l.fade, 0, 127)];
        const double blocks = fadeMs > 0.0 ? std::max(1.0, std::round(fadeMs / 1000.0 * fs / ctl)) : 0.0;
        d.lfoFadeStep[k] = blocks > 0.0 ? 1.0 / blocks : 1.0;
        d.lfoReset[k] = l.phase > 180 ? -1.0 : ((l.phase + 360) % 360) / 360.0;
    }
}

// an operator's mode settings (eng_opx.c opx_sync) from A, B, C, D plus the routes' offsets da..dd, in the values'
// own units. With no offset the arithmetic is the firmware's (its integer divisions included); moved, it is
// continuous: a route sweeps FB, WIDTH, a fold's GAIN, a filter's CUTOFF and RESO, an effect's E and F smoothly
Engine::OpCoef Engine::opCoef(const OpParams& o, int fbOffset, double da, double db, double dc, double dd)
{
    OpCoef c;
    const double A = o.a + da, B = o.b + db, C = o.c + dc, D = o.d + dd;
    if (o.mode == FM) {
        const double pct = std::clamp(A + fbOffset, 0.0, 100.0);
        if (da == 0.0) {
            c.fbk = fbGain((int)pct);
        } else {                                         // (opx_fbk, continuous: to a saw at 75 %, then to noise)
            const double q = pct <= 75.0 ? pct * 10925.0 / 75.0 : 10925.0 + (pct - 75.0) * 437.0;
            c.fbk = (float)(q / 131072.0);
        }
        if (B >= 1.0 && B <= 99.0) {                     // WIDTH: the wave squeezed into b % of the cycle
            c.wlim = (float)(B / 100.0);
            c.winv = (float)(100.0 / B);
        }
    } else if (o.mode == RING) {
        c.a = (float)(std::clamp(A, 0.0, 100.0) / 100.0);   // DEPTH
        c.b = (float)(std::clamp(B, 0.0, 100.0) / 100.0);   // SHAPE
    } else if (o.mode == FOLD) {
        c.a = (float)(std::clamp(B, 0.0, 100.0) / 100.0);   // OSCMIX
        c.b = (float)(1.0 + std::clamp(A, 0.0, 100.0) * 15.0 / 100.0);   // GAIN: a drive of 1 .. 16
        c.bias = (float)((std::clamp(C, 0.0, 200.0) - 100.0) / 100.0);
    } else if (o.mode == FILTER || o.mode == FILTFM || o.mode == EFFECT) {
        const int n = o.mode == EFFECT ? kNumFx : 12;
        c.ftype = ((o.a % n) + n) % n;                   // A: TYPE (FILTER), FX (EFFECT): a list, not moved
        c.a = (float)(std::clamp(B, 0.0, 100.0) / 100.0);   // B: OSCMIX
        c.fe = std::clamp(C, 0.0, 200.0);                // C: CUTOFF / E (FREQ: half semitones)
        c.fres = std::clamp(D, 0.0, 100.0);              // D: RESO / F
        c.foff = (c.fe - 100.0) / 24.0;
    } else if (o.mode == MODAL) {                        // (FmModal.h) A: STRUCTURE, a list; B: OSC MIX; C: SIZE; D: DECAY
        c.ftype = ((o.a % kModalStructures) + kModalStructures) % kModalStructures;
        c.a = (float)(std::clamp(B, 0.0, 100.0) / 100.0);
        c.fe = std::clamp(C, 0.0, 200.0);                // (as CUTOFF: half semitones from the pitch, 100 = on it)
        c.fres = std::clamp(D, 0.0, 100.0);
        c.foff = (c.fe - 100.0) / 24.0;
    } else if (o.mode == SAMPLE) {                       // (FmSample.h) A: READ, a list; B: START; C: LENGTH; D: XFADE
        c.ftype = ((o.a % kSampleReads) + kSampleReads) % kSampleReads;
        c.b = (float)std::clamp(B, 0.0, 100.0);
        c.fe = std::clamp(C, 0.0, 200.0);
        c.fres = std::clamp(D, 0.0, 100.0);
    } else if (o.mode == CHAOS || o.mode == STOCH || o.mode == MACRO) {   // (FmChaos.h, FmMacro.h) CHAOS: A MODEL,
        // B CHAOS, C SHAPE, D SMOOTH; STOCHASTIC: A DISTRIBUTION, B LEVEL STEP, C POINTS, D LENGTH STEP; MACRO: A MODEL,
        // B HARMONICS, C TIMBRE, D MORPH
        const int n = o.mode == CHAOS ? kChaosModels : o.mode == STOCH ? kStochDists : kMacroModels;
        c.ftype = ((o.a % n) + n) % n;                   // C POINTS, D LENGTH STEP
        c.b = (float)std::clamp(B, 0.0, 100.0);
        c.fe = std::clamp(C, 0.0, 200.0);
        c.fres = std::clamp(D, 0.0, 100.0);
    }
    return c;
}

// the routes for one voice, once a control block: the 24 sources' values for it, then each route's offset on its
// destination (amount x the source x the value's range, summed), in the destination's own units
void Engine::modBlock(Voice& v, double env1, double env2, double env3)
{
    double s[kNumSources] {};
    s[MS_VEL] = v.vel / 127.0;
    s[MS_AT] = std::clamp(ctlAt + (v.channel >= 0 ? (v.expressionFrozen ? v.releasePressure : channelPressure[v.channel]) : 0.0), 0.0, 1.0);
    s[MS_TIMBRE] = v.channel >= 0 ? std::clamp(timbre + (v.expressionFrozen ? v.releaseTimbre : channelTimbre[v.channel]) - .5, 0.0, 1.0) : timbre;
    s[MS_MW] = ctlMw;
    s[MS_KEY] = (v.note - 60) / 60.0;                    // (bipolar: +-1 five octaves from C4)
    s[MS_EXPR] = ctlExpr;
    s[MS_ENV1] = env1;
    s[MS_ENV2] = env2;
    s[MS_ENV3] = env3;
    for (int k = 0; k < kNumLfos; ++k) s[MS_LFO1 + k] = v.lfo[k];
    for (int m = 0; m < 4; ++m) s[MS_MAC1 + m] = macroSrc[m];
    for (int m = 0; m < 4; ++m) s[MS_SEQ1 + m] = seqSrc[m];
    for (int m = 0; m < 8; ++m) s[MS_CTRL1 + m] = ctrlSrc[m];
    for (int k = 0; k < 6; ++k)                          // each operator's envelope (OP k: the voice keeps them reversed)
        s[MS_OPENV1 + k] = std::clamp(v.op[(size_t)(5 - k)].env.lvl, 0.0, 1.0);
    // FUNC (this voice's, from its note), RAND (the engine's), COMB (this voice's: from the sources above, a COMB only
    // from the ones before it, never itself)
    const double blockSec = ctl / fs;
    const auto xm = [&](int x) { return v.mod[(size_t)extIndex(x)]; };   // (the routes on the sources' own settings)
    for (int k = 0; k < 3; ++k) {
        const auto& f = patch.func[k];
        const auto& sh = funcShapes[(size_t)k];
        const int fb = XV_F1MODE + k * kFuncValues;
        const double speed = std::clamp(f.speed + xm(fb + 1), 0.0, 127.0), smooth = std::clamp(f.smooth + xm(fb + 3), 0.0, 100.0);
        // (SYNC: the whole curve once a division of the host's tempo; free: SPEED's 0.01 .. 100 Hz)
        const double hz = f.sync > 0 ? tpBpm / 60.0 / kSyncBeats[std::clamp(f.sync, 1, 14)]
                                     : 0.01 * std::pow(10000.0, speed / 127.0);
        const double susX = sh.p[(size_t)std::clamp(f.sustain, 0, std::max(0, sh.n - 1))].x;
        const double sk = smooth > 0.0 ? 1.0 - std::exp(-blockSec / (smooth / 1000.0)) : 1.0;
        s[MS_FUNC1 + k] = funcBlock(v.func[(size_t)k], sh, f.mode, hz * blockSec, susX, v.gate, f.polarity != 0, sk);
    }
    for (int k = 0; k < 3; ++k) s[MS_RAND1 + k] = randVal[k];
    for (int k = 0; k < 3; ++k) {
        const auto& c = patch.comb[k];
        const auto in = [&](int x) { return x > 0 && x < kNumSources && !(x >= MS_COMB1 + k && x <= MS_COMB3) ? s[x] : 0.0; };
        const double amt = std::clamp(c.amount + xm(XV_C1TYPE + k * kCombValues + 3), -100.0, 100.0);
        s[MS_COMB1 + k] = combBlock(c.type, in(c.src), in(c.mod), amt / 100.0, v.combSt[k], blockSec, inputLevel);
    }
    // the engine-wide ones (the common LFOs' SPEED, RAND's settings) follow the newest voice's routes
    if (v.age == liveAge) {
        for (int k = 0; k < kNumLfos; ++k) lfoRateMul[k] = lfoSpeedMul(v, k);
        for (int k = 0; k < 3; ++k)
            for (int j = 0; j < 5; ++j) randMod[k][j] = xm(XV_R1TYPE + k * kRandValues + 1 + j);
    }
    // RETRIG (M4): a source's rise past 0.5 restarts an LFO (its own phase, or the common one from the newest voice)
    // or strikes an envelope again from where it is - while the key is down (a released note never comes back), and
    // never by itself (an LFO by its own value, ENV 1..3 by their own)
    if (v.gate) {
        const auto rising = [&](int slot, int src, int self) {
            if (src <= MS_NONE || src >= kNumSources || src == self) return false;
            const bool hi = s[src] > 0.5, rise = hi && !v.trigHigh[slot];
            v.trigHigh[slot] = hi;
            return rise;
        };
        for (int k = 0; k < kNumLfos; ++k) {
            const auto& l = patch.lfo[k];
            if (!rising(k, l.retrig, MS_LFO1 + k)) continue;
            const double reset = d.lfoReset[k] < 0.0 ? (xorshift(v.rng) >> 8) / 16777216.0 : d.lfoReset[k];
            if (l.keySync == 2) v.lph[k] = reset;
            else if (v.age == liveAge) lph[k] = reset;
        }
        for (int k = 0; k < 3; ++k)                      // FUNC k from its start (this voice's)
            if (rising(kNumLfos + kNumEnvs + k, patch.func[k].retrig, MS_FUNC1 + k)) v.func[(size_t)k].start();
        if (v.age == liveAge)                            // RAND k (the engine's): a step at once, a loop from its first
            for (int k = 0; k < 3; ++k) {
                const int src = patch.rand[k].retrig;
                if (src <= MS_NONE || src >= kNumSources || src == MS_RAND1 + k) continue;
                const bool hi = s[src] > 0.5;
                if (hi && !randTrigHigh[k]) rands[(size_t)k].restart();
                randTrigHigh[k] = hi;
            }
        for (int e = 0; e < kNumEnvs; ++e) {
            const Adsr a = e < 6 ? opAdsr(v, e) : e == EV_EG1 ? eg1Adsr(v) : e == EV_EG2 ? eg2Adsr(v) : env1Adsr(v);
            const int self = e == EV_EG1 ? MS_ENV2 : e == EV_EG2 ? MS_ENV3 : e == EV_ENV1 ? MS_ENV1 : -1;
            if (!rising(kNumLfos + e, a.retrig, self)) continue;
            Env& en = e < 6 ? v.op[(size_t)(5 - e)].env : e == EV_EG1 ? v.eg1 : e == EV_EG2 ? v.eg2 : v.env1;
            envGo(en, Env::Atk, a);
        }
    }
    if (v.age == liveAge)                               // (the newest voice: what the editor draws)
        for (int i = 1; i < kNumSources; ++i) live[(size_t)i].store((float)s[i], std::memory_order_relaxed);
    applyRoutes(v, s);
}

// each route's offset on its destination: amount x the source x the value's range, summed
void Engine::applyRoutes(Voice& v, const double* s)
{
    const auto& sch = schema();
    for (int i = 0; i < routes.n; ++i) v.mod[(size_t)routes.r[(size_t)i].dst] = 0.0;
    for (int i = 0; i < routes.n; ++i) {
        const Route& r = routes.r[(size_t)i];
        const auto& dsc = sch[(size_t)r.dst];
        v.mod[(size_t)r.dst] += r.amount * s[r.src] * (dsc.max - dsc.min);
    }
}

// at a note, before its envelopes start: the routes of the sources a note already has (its velocity and key, the
// controllers, the macros, the engine's RAND and common LFOs) - so a velocity on a DECAY, a key on an ATTACK, take
// the stage that begins now. The envelopes, FUNC and COMB begin at 0, as they are then
void Engine::noteOnRoutes(Voice& v)
{
    if (routes.n == 0) return;
    double s[kNumSources] {};
    s[MS_VEL] = v.vel / 127.0;
    s[MS_AT] = std::clamp(ctlAt + (v.channel >= 0 ? channelPressure[v.channel] : 0.0), 0.0, 1.0);
    s[MS_TIMBRE] = v.channel >= 0 ? std::clamp(timbre + channelTimbre[v.channel] - .5, 0.0, 1.0) : timbre;
    s[MS_MW] = ctlMw;
    s[MS_KEY] = (v.note - 60) / 60.0;
    s[MS_EXPR] = ctlExpr;
    for (int k = 0; k < kNumLfos; ++k) s[MS_LFO1 + k] = patch.lfo[k].keySync == 2 ? 0.0 : lfoValue(k, lph[k]);
    for (int k = 0; k < 3; ++k) s[MS_RAND1 + k] = randVal[k];
    for (int m = 0; m < 4; ++m) s[MS_MAC1 + m] = macroSrc[m];
    for (int m = 0; m < 4; ++m) s[MS_SEQ1 + m] = seqSrc[m];   // (the lanes, as they stand at the note)
    for (int m = 0; m < 8; ++m) s[MS_CTRL1 + m] = ctrlSrc[m];
    applyRoutes(v, s);
}

Adsr Engine::movedAdsr(const Voice& v, const Adsr& a, int i0, int iAc) const
{
    const auto m = [&](int i) { return v.mod[(size_t)i]; };
    if (m(i0) == 0.0 && m(i0 + 1) == 0.0 && m(i0 + 2) == 0.0 && m(i0 + 3) == 0.0 && m(i0 + 4) == 0.0 && m(iAc) == 0.0)
        return a;                                        // (no route: the patch's, bit for bit)
    Adsr r = a;
    const auto mv = [&](int base, int i, int lo, int hi) { return std::clamp((int)std::lround(base + m(i)), lo, hi); };
    r.atk = mv(a.atk, i0, 0, 127);
    r.dec = mv(a.dec, i0 + 1, 0, 127);
    r.sus = mv(a.sus, i0 + 2, 0, 100);
    r.rel = mv(a.rel, i0 + 3, 0, 127);
    r.curve = mv(a.curve, i0 + 4, 0, 10);
    r.atkCurve = mv(a.atkCurve, iAc, 0, 10);
    return r;
}
Adsr Engine::opAdsr(const Voice& v, int op) const
{
    return movedAdsr(v, patch.op[(size_t)op].adsr(), opIndex(op, OV_ATK),
                     extIndex(XV_EV1MODE + (EV_OP1 + op) * kEnvXValues + 1));
}
Adsr Engine::eg1Adsr(const Voice& v) const
{
    return movedAdsr(v, patch.eg1, globalIndex(GV_E1ATK), extIndex(XV_EV1MODE + EV_EG1 * kEnvXValues + 1));
}
Adsr Engine::eg2Adsr(const Voice& v) const
{
    return movedAdsr(v, patch.flt.eg2, globalIndex(GV_E2ATK), extIndex(XV_EV1MODE + EV_EG2 * kEnvXValues + 1));
}
Adsr Engine::env1Adsr(const Voice& v) const
{
    return movedAdsr(v, patch.env1, extIndex(XV_ENV1ATK), extIndex(XV_EV1MODE + EV_ENV1 * kEnvXValues + 1));
}

// SPEED 0..127 maps 0.01 .. 100 Hz exponentially: a route's offset of d steps multiplies the rate by 10000^(d / 127)
double Engine::lfoSpeedMul(const Voice& v, int k) const
{
    const int i = k == 0 ? globalIndex(GV_L1SPD) : k == 1 ? globalIndex(GV_L2SPD) : extIndex(XV_L3SPD);
    const double d = v.mod[(size_t)i];
    return d == 0.0 ? 1.0 : std::pow(10000.0, std::clamp(d, -127.0, 127.0) / 127.0);
}

ModalSettings Engine::modalSettings(const Voice& v, int k) const
{
    const int op = 5 - k;
    const OpParams& po = patch.op[(size_t)op];
    const Op& o = v.op[(size_t)k];
    const auto mo = [&](int x) { return v.mod[(size_t)modalIndex(op, x)]; };
    ModalSettings s;
    s.structure = v.oc[(size_t)k].ftype;
    s.material = po.material;
    const double cOff = v.oc[(size_t)k].foff - d.oc[(size_t)k].foff;   // (SIZE moved by a route: on the slid one)
    s.f1 = std::exp2(o.lfk + fofs[(size_t)k] + cOff);
    s.decay = std::exp2((v.oc[(size_t)k].fres - 50.0) / 10.0);          // DECAY 0..100: x1/32 .. x32 the material's
    s.bright = std::clamp(po.bright + mo(1), -100.0, 100.0) / 100.0;
    s.stiff = std::clamp(po.stiff + mo(2), -100.0, 100.0) / 100.0;
    s.position = std::clamp(po.position + mo(3), 0.0, 100.0) / 100.0;
    s.density = std::clamp(po.density, 1, kModalModes);
    s.irreg = std::clamp(po.irreg + mo(5), 0.0, 100.0) / 100.0;
    s.strike = std::clamp(po.strike + mo(6), 0.0, 100.0) / 100.0;
    s.hardness = std::clamp(po.hardness + mo(7), 0.0, 100.0) / 100.0;
    s.exciter = po.exciter;
    s.pressure = std::clamp(po.pressure + mo(9), 0.0, 100.0) / 100.0;
    s.tension = std::clamp(po.tension + mo(10), 0.0, 100.0) / 100.0;
    s.rattle = std::clamp(po.rattle + mo(11), 0.0, 100.0) / 100.0;
    return s;
}

void Engine::setRoutes(const Routes& r)
{
    routes.n = 0;                                        // (only the sound ones: a known source, a movable value)
    for (int i = 0; i < r.n && routes.n < kMaxRoutes; ++i) {
        const Route& x = r.r[(size_t)i];
        if (x.src > MS_NONE && x.src < kNumSources && isModulatable(x.dst) && x.amount != 0.0f)
            routes.r[(size_t)routes.n++] = x;
    }
    for (auto& v : voices) v.mod.fill(0.0);              // (a route removed leaves nothing behind)
}

void Engine::setControllers(double aftertouch, double modWheel, double expression)
{
    ctlAt = std::clamp(aftertouch, 0.0, 1.0);
    ctlMw = std::clamp(modWheel, 0.0, 1.0);
    ctlExpr = std::clamp(expression, 0.0, 1.0);
    live[MS_AT].store((float)ctlAt, std::memory_order_relaxed);   // (shown live with no note too)
    live[MS_MW].store((float)ctlMw, std::memory_order_relaxed);
    live[MS_EXPR].store((float)ctlExpr, std::memory_order_relaxed);
}

void Engine::setMacroSources(const float m[4])
{
    for (int i = 0; i < 4; ++i) {
        macroSrc[i] = std::clamp(m[i], -1.0f, 1.0f);
        live[(size_t)MS_MAC1 + (size_t)i].store(macroSrc[i], std::memory_order_relaxed);
    }
}

void Engine::setCtrlSources(const float s[8])
{
    for (int i = 0; i < 8; ++i) {
        ctrlSrc[i] = std::clamp(s[i], 0.0f, 1.0f);
        live[(size_t)MS_CTRL1 + (size_t)i].store(ctrlSrc[i], std::memory_order_relaxed);
    }
}

void Engine::setSeqSources(const float s[4])
{
    for (int i = 0; i < 4; ++i) {
        seqSrc[i] = std::clamp(s[i], 0.0f, 1.0f);
        live[(size_t)MS_SEQ1 + (size_t)i].store(seqSrc[i], std::memory_order_relaxed);
    }
}

void Engine::setFunctionShape(int k, const FuncShape& s)
{
    if (k < 0 || k >= 3) return;
    funcShapes[(size_t)k] = sanitizeFuncShape(s);
}

void Engine::setInputLevel(double level) { inputLevel = std::clamp(level, 0.0, 1.0); }

void Engine::lfoRates()
{
    for (int k = 0; k < kNumLfos; ++k) {
        const auto& l = patch.lfo[k];
        const double hz = l.sync > 0 ? tpBpm / 60.0 / kSyncBeats[std::clamp(l.sync, 1, 14)]
                                     : 0.01 * std::pow(10000.0, std::clamp(l.speed, 0, 127) / 127.0);
        d.lfoCyc[k] = hz * ctl / fs;
    }
}

void Engine::setTransport(double bpm, double ppq, bool playing)
{
    const double b = std::isfinite(bpm) && bpm >= 20.0 && bpm <= 999.0 ? bpm : 120.0;
    if (b != tpBpm) {
        tpBpm = b;
        lfoRates();
    }
    // where the transport will be at the engine's next control block (the block under way ends first)
    tpPpq = std::isfinite(ppq) ? ppq + (engPos ? ctl - engPos : 0) * tpBpm / 60.0 / fs : 0.0;
    tpPlaying = playing;
}

// LFO k's value at phase ph: SYMMETRY bends time (the first half of the cycle over (1 + s) / 2 of it), MORPH fades
// into WAVE B, POLARITY sets 0..1 or -1..1 (AS WAVE: the wave's own; WAVE B then follows it). All at their defaults:
// lfoWave as it is (the firmware's)
double Engine::lfoValue(int k, double ph) const { return lfoValue(k, ph, 0.0, 0.0); }

// (dSym, dMorph: a voice's routes on SYMMETRY and MORPH, in their own units; 0, 0: the patch's)
double Engine::lfoValue(int k, double ph, double dSym, double dMorph) const
{
    const auto& l = patch.lfo[k];
    const double sym = std::clamp(l.symmetry + dSym, -100.0, 100.0), morph = std::clamp(l.morph + dMorph, 0.0, 100.0);
    if (sym != 0.0) {
        const double m = std::clamp(0.5 * (1.0 + sym / 100.0), 0.02, 0.98);
        ph = ph < m ? 0.5 * ph / m : 0.5 + 0.5 * (ph - m) / (1.0 - m);
    }
    const auto uni = [](int w) {
        w = ((w % kNumLfoWaves) + kNumLfoWaves) % kNumLfoWaves;
        return w == 6 || (w >= 8 && w <= 11);
    };
    double v = lfoWave(l.wave, ph, lr[k]);
    if (morph <= 0.0 && l.polarity == 0) return v;
    const bool wantUni = l.polarity == 1 || (l.polarity == 0 && uni(l.wave));
    const auto conv = [&](double x, bool isUni) { return isUni == wantUni ? x : wantUni ? 0.5 * (x + 1.0) : 2.0 * x - 1.0; };
    v = conv(v, uni(l.wave));
    if (morph > 0.0) {
        const double b = conv(lfoWave(l.waveB, ph, lr[k]), uni(l.waveB));
        v += (b - v) * (morph / 100.0);
    }
    return v;
}

// an LFO's value at phase ph (0..1 cycles), opx_lfo_wave: the + waves 0..1 (and GUITAR, a vibrato up only), the rest
// -1..1
double Engine::lfoWave(int wave, double ph, const LfoRandom& r)
{
    const double tri = ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph;
    const auto x = [](double u) { return 2.0 * u * u - 1.0; };                     // the exponential: 2 u^2 - 1
    const auto steps = [](double v, int n) {                                         // n levels over -1..1
        const int k = std::min(n - 1, (int)((v + 1.0) * 0.5 * n));
        return k * 2.0 / (n - 1) - 1.0;
    };
    const int w = ((wave % kNumLfoWaves) + kNumLfoWaves) % kNumLfoWaves;
    double v;
    switch (w) {
    case 1: case 9: v = 1.0 - 2.0 * ph; break;                                      // SAW DOWN
    case 2: case 10: v = 2.0 * ph - 1.0; break;                                     // SAW UP
    case 3: case 11: v = ph < 0.5 ? 1.0 : -1.0; break;                              // SQR
    case 4: v = std::sin(2.0 * kPi * ph); break;                                    // SIN
    case 5: v = r.held; break;                                                      // S/H
    case 6: return 0.5 * (1.0 - std::cos(2.0 * kPi * ph));                          // GUITAR (unipolar)
    case 7: v = r.prev + (r.held - r.prev) * ph; break;                             // RANDOM: glides
    case 12: v = x((tri + 1.0) * 0.5); break;                                       // EXP TRI
    case 13: v = x(1.0 - ph); break;                                                // EXP SAW DN
    case 14: v = x(ph); break;                                                      // EXP SAW UP
    case 15: v = steps(tri, 4); break;
    case 16: v = steps(tri, 6); break;
    case 17: v = steps(1.0 - 2.0 * ph, 4); break;
    case 18: v = steps(1.0 - 2.0 * ph, 6); break;
    case 19: v = r.tp; break;                                                       // S.RND TIME
    case 20: v = r.lv; break;                                                       // S.RND LV+TIME
    case 21: v = r.tpp + (r.tp - r.tpp) * ph; break;                                // RND TIME
    case 22: v = r.lvp + (r.lv - r.lvp) * ph; break;                                // RND LV+TIME
    case 23: v = r.logP + (r.logV - r.logP) * ph; break;                            // CHAOS LOGISTIC (glides)
    case 24: v = r.henP + (r.henV - r.henP) * ph; break;                            // CHAOS HENON
    case 25: v = r.lorP + (r.lorV - r.lorP) * ph; break;                            // CHAOS LORENZ
    default: v = tri; break;                                                        // TRI, TRI +
    }
    v = std::clamp(v, -1.0, 1.0);
    return w >= 8 && w <= 11 ? 0.5 * (v + 1.0) : v;
}

std::vector<double> Engine::lfoTrace(int wave, int cycles, int per)
{
    LfoRandom r;
    uint32_t rnd = 0x12345678u;
    std::vector<double> out;
    out.reserve((size_t)(cycles * per));
    for (int c = 0; c < cycles; ++c) {
        lfoCycle(r, rnd);
        for (int i = 0; i < per; ++i) out.push_back(lfoWave(wave, (double)i / per, r));
    }
    return out;
}

// a new cycle: S/H draws its value; the random-in-time ones flip (a coin) and renew their level (another)
void Engine::lfoCycle(LfoRandom& r, uint32_t& rnd)
{
    r.prev = r.held;
    r.held = (double)((int32_t)(xorshift(rnd) >> 16) - 32768) / 32768.0;
    r.tpp = r.tp;
    r.lvp = r.lv;
    if (!r.rnd) r.rnd = 0x9E3779B9u;
    const uint32_t c = xorshift(r.rnd);
    if (r.tp == 0.0) r.tp = 1.0;
    if (c & 1u) r.tp = -r.tp;
    if (c & 2u) r.lv = (double)((int32_t)(xorshift(r.rnd) >> 16) - 32768) / 32768.0;
    // the chaotic waves: each map a step (all of them, a few operations: the wave may change at any cycle)
    r.logP = r.logV;
    r.logX = 3.9 * r.logX * (1.0 - r.logX);
    if (!(r.logX > 1e-6 && r.logX < 1.0 - 1e-6)) r.logX = 0.3;   // (off the edge or stuck: started again)
    r.logV = 2.0 * r.logX - 1.0;
    r.henP = r.henV;
    {
        const double x = 1.0 - 1.4 * r.henX * r.henX + r.henY;
        r.henY = 0.3 * r.henX;
        r.henX = x;
        if (!(std::fabs(r.henX) < 2.0)) r.henX = r.henY = 0.1;    // (escaped: back on the attractor's basin)
    }
    r.henV = std::clamp(r.henX / 1.3, -1.0, 1.0);
    r.lorP = r.lorV;
    for (int i = 0; i < 8; ++i) {
        const double dx = 10.0 * (r.lorY - r.lorX), dy = r.lorX * (28.0 - r.lorZ) - r.lorY, dz = r.lorX * r.lorY - 8.0 / 3.0 * r.lorZ;
        r.lorX += 0.01 * dx;
        r.lorY += 0.01 * dy;
        r.lorZ += 0.01 * dz;
    }
    if (!std::isfinite(r.lorX + r.lorY + r.lorZ)) r.lorX = r.lorY = r.lorZ = 1.0;
    r.lorV = std::clamp(r.lorX / 20.0, -1.0, 1.0);
}

// an envelope (0..1) and an LFO (-1..1) times their depths (1/16 semitone) -> octaves. The firmware's arithmetic
// rounds each product DOWN to whole 1/16 semitones (an arithmetic shift): a small depth's vibrato is then lopsided,
// a constant flat offset of ~1/32 semitone on average, that detunes an operator against the others and drifts their
// phases (CLARINETE, 20/10: its level wanders 5 dB). The A/B (fw2x) keeps that; the native engine does not
double Engine::pitchMod(double env, double envDepth, double lfo, double lfoDepth) const
{
    if (!fw2x) return (env * envDepth + lfo * lfoDepth) / 192.0;
    const double lq = std::round(lfo * 32767.0);                       // (the LFO as the firmware's Q15)
    return (std::floor(env * envDepth) + std::floor(lq * lfoDepth / 32768.0)) / 192.0;
}

// once a control block (opx_block): the common LFOs advance; a new cycle moves their random values on
void Engine::engineBlock()
{
    bool anyVoice = false;
    for (const auto& v : voices) anyVoice = anyVoice || v.active;
    if (!anyVoice) {                                     // (no voice: no route moves the engine-wide ones)
        for (double& m : lfoRateMul) m = 1.0;
        for (auto& r : randMod) for (double& m : r) m = 0.0;
    }
    for (int k = 0; k < kNumLfos; ++k) {
        const auto& l = patch.lfo[k];
        if (l.sync > 0 && l.keySync == 0 && tpPlaying) {   // SYNC, free, the host playing: its phase is the bar's
            const double r = d.lfoReset[k] > 0.0 ? d.lfoReset[k] : 0.0;
            double ph = tpPpq / kSyncBeats[std::clamp(l.sync, 1, 14)] + r;
            ph -= std::floor(ph);
            if (ph < lph[k]) lfoCycle(lr[k], lrnd);       // (a new cycle: the random waves move on)
            lph[k] = ph;
            continue;
        }
        lph[k] += d.lfoCyc[k] * (l.sync > 0 ? 1.0 : lfoRateMul[k]);   // (SPEED moved: the newest voice's routes)
        if (lph[k] >= 1.0) {
            lph[k] -= std::floor(lph[k]);
            lfoCycle(lr[k], lrnd);
        }
    }
    tpPpq += ctl / fs * tpBpm / 60.0;
    // no voice sounding: the free and common LFOs still run - shown live as they are (a voice's own phase, KEY SYNC
    // VOICE, has none to show: 0). With a voice, modBlock shows the newest one's (its fade included)
    if (!anyVoice)
        for (int k = 0; k < kNumLfos; ++k)
            live[(size_t)MS_LFO1 + (size_t)k].store(patch.lfo[k].keySync == 2 ? 0.0f : (float)lfoValue(k, lph[k]),
                                                    std::memory_order_relaxed);
    // RAND 1..3: the engine's, ticking at their SPEED; RISE and FALL their smoothing up and down (time indices)
    const auto& T = Tables::get();
    const double blockSec = ctl / fs;
    const auto slew = [&](int idx) {
        const double t = T.envMs[(size_t)std::clamp(idx, 0, 127)] / 1000.0;
        return t > 0.0 ? 1.0 - std::exp(-blockSec / t) : 1.0;
    };
    for (int k = 0; k < 3; ++k) {                        // (SPEED, A, B, RISE, FALL moved by the newest voice's routes)
        const auto& r = patch.rand[k];
        const auto& m = randMod[k];
        const double hz = 0.01 * std::pow(10000.0, std::clamp(r.speed + m[0], 0.0, 127.0) / 127.0);
        const int a = std::clamp((int)std::lround(r.a + m[1]), 0, 100), b = std::clamp((int)std::lround(r.b + m[2]), 0, 100);
        const int rise = (int)std::lround(r.rise + m[3]), fall = (int)std::lround(r.fall + m[4]);
        double inc = hz * blockSec;
        if (r.sync > 0) {                                // SYNC: a step a division; the host playing: on its grid
            const double beats = kSyncBeats[std::clamp(r.sync, 1, 14)];
            if (tpPlaying) {
                const double ppqNow = tpPpq - blockSec * tpBpm / 60.0;   // (tpPpq already moved on, above)
                double want = ppqNow / beats;
                want -= std::floor(want);
                inc = want - rands[(size_t)k].phase;
                if (inc < 0.0) inc += 1.0;
            } else {
                inc = tpBpm / 60.0 / beats * blockSec;
            }
        }
        randVal[k] = rands[(size_t)k].block(r.type, inc, a, b, slew(rise), slew(fall));
        live[(size_t)MS_RAND1 + (size_t)k].store((float)randVal[k], std::memory_order_relaxed);   // (no note needed)
    }
}

void Engine::algorithmLinks(int alg, int algFb, float links[6][6], unsigned& carriers)
{
    // derive's own path, so the editor shows what the engine plays (an engine is large: on the heap; the editor
    // asks only when the algorithm changes, never the audio thread)
    auto e = std::make_unique<Engine>();
    e->patch.alg = std::clamp(alg, 0, kNumAlgs - 1);
    e->patch.algFb = algFb;
    e->derive();
    carriers = 0;
    for (int j = 0; j < 6; ++j) {
        for (int k = 0; k < 6; ++k) links[5 - j][5 - k] = e->d.mx[(size_t)j][(size_t)k];
        if ((e->d.dout >> j) & 1) carriers |= 1u << (5 - j);
    }
}

// enter stage st; a stage of 0 ms is passed at once (opx_env_go)
void Engine::envGo(Env& e, Env::Stage st, const Adsr& o) const
{
    const auto& T = Tables::get();
    for (;;) {
        int t;
        e.st = st;
        e.curve = st == Env::Rel && o.relCurve >= 0 ? o.relCurve : o.curve;   // (REL CURVE -1: CURVE's)
        e.atkCurve = o.atkCurve;
        if (st == Env::Atk) { t = o.atk; e.target = 1.0; }
        else if (st == Env::Dec) { t = o.dec; e.target = o.sus >= 100 ? 1.0 : o.sus / 100.0; }
        else if (st == Env::Rel) { t = o.rel; e.target = 0.0; }
        else return;
        const double ms = T.envMs[(size_t)std::clamp(t, 0, 127)] * e.timeScale;
        e.n = ms > 0.0 ? std::max<int64_t>(1, std::llround(ms / 1000.0 * fs / ctl)) : 0;   // in control blocks
        if (e.n) {
            e.step = (e.target - e.lvl) / (double)e.n;
            e.k = std::pow(1000.0, -1.0 / (double)e.n);   // the distance to -60 dB in the stage's time
            e.n0 = e.n;
            e.start = e.lvl;
            return;
        }
        e.lvl = e.target;
        // (ADR: no sustain - the decay's end lets go at once. LOOP: back to the attack - if the attack takes time; both
        // at 0, it holds at the sustain, never a loop that takes no time)
        st = st == Env::Atk ? Env::Dec
           : st == Env::Dec ? (o.mode == 1 ? Env::Rel : o.mode == 2 && o.atk > 0 ? Env::Atk : Env::Sus)
                            : Env::Off;
    }
}

void Engine::envRelease(Env& e, const Adsr& o) const
{
    if (o.mode != 1) envGo(e, Env::Rel, o);
}

// one control block: the attack linear; the decay and the release a linear ramp and an exponential mixed by the curve
double Engine::envTick(Env& e, const Adsr& o) const
{
    switch (e.st) {
    case Env::Atk:
        if (e.atkCurve <= 0) {
            e.lvl += e.step;                             // (the firmware's: linear)
        } else {                                         // ATK CURVE: toward 1 - (1 - t)^3, fast then slow
            const double t = 1.0 - (double)(e.n - 1) / (double)std::max<int64_t>(1, e.n0);
            const double sh = t + std::min(e.atkCurve, 10) / 10.0 * ((1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t)) - t);
            e.lvl = e.start + (1.0 - e.start) * sh;
        }
        if (--e.n <= 0) { e.lvl = 1.0; envGo(e, Env::Dec, o); }
        break;
    case Env::Dec:
    case Env::Rel: {
        const double lin = e.lvl + e.step, ex = e.target + (e.lvl - e.target) * e.k;
        e.lvl = lin + (ex - lin) * (e.curve / 10.0);
        if ((e.step < 0 && e.lvl < e.target) || (e.step > 0 && e.lvl > e.target)) e.lvl = e.target;
        if (--e.n <= 0) {
            e.lvl = e.target;
            if (e.st == Env::Dec && o.mode == 1) envGo(e, Env::Rel, o);   // (ADR: on to the release)
            else if (e.st == Env::Dec && o.mode == 2) envGo(e, Env::Atk, o);   // (LOOP: the attack again, held)
            else e.st = e.st == Env::Dec ? Env::Sus : Env::Off;
        }
        break;
    }
    default: break;
    }
    if (e.lvl < 0.0) e.lvl = 0.0;
    return e.lvl;
}

void Engine::noteOn(int note, int velocity, int channel)
{
    smp = sampleSlot.take();                              // (SAMPLE: a recording given just before the note is its)
    // a free voice, else the oldest released, else the oldest
    Voice* v = nullptr;
    for (auto& c : voices) if (!c.active) { v = &c; break; }
    if (!v) for (auto& c : voices) if (!c.gate && (!v || c.age < v->age)) v = &c;
    if (!v) for (auto& c : voices) if (!v || c.age < v->age) v = &c;
    const bool fresh = !v->active;
    bool held = false;                                    // (another key down: KEY SYNC COMMON restarts only from none)
    for (const auto& c : voices) held = held || (c.active && c.gate && &c != v);
    v->ctlPos = 0;                                        // (the note starts its own control block: its pitch at once)
    v->active = v->gate = true;
    v->note = note;
    v->channel = channel >= 0 && channel < 16 ? channel : -1;
    v->expressionFrozen = false;
    if(v->channel >= 0)channelPitchSmooth[v->channel]=channelPitch[v->channel];
    v->vel = std::clamp(velocity, 0, 127);
    v->age = ++ageCounter;
    if (fresh) v->mod.fill(0.0);
    noteOnRoutes(*v);                                     // (the envelopes below begin with the note's routes)
    const double ph0 = patch.phase == 1 ? (xorshift(v->rng) >> 8) / 16777216.0 : 0.0;
    for (int k = 0; k < 6; ++k) {
        Op& o = v->op[(size_t)k];
        const OpParams& po = patch.op[(size_t)(5 - k)];
        if (fresh) {
            o.y0 = o.y1 = 0.0f;
            o.nz[0] = o.nz[1] = o.nz[2] = 0.0f;
            o.foldX1 = 0.0f;
            o.f = FState {};
            o.fx.clear();
            o.modal.clear();
            o.env.lvl = 0.0;
            o.g = o.dg = o.gEnd = 0.0;                    // (from silence: the gain ramps up from 0)
            o.eg = o.deg = o.egEnd = 0.0;
        }
        if (d.mode[(size_t)k] == CHAOS) {                 // (CHAOS: each note from the map's start - playable, the same)
            const OpCoef& c = d.oc[(size_t)k];
            o.chaos.start(c.ftype, c.b, c.fe, xorshift(v->rng));
        } else if (d.mode[(size_t)k] == STOCH) {          // (STOCHASTIC: each note a new cycle of points)
            o.stoch.start(d.oc[(size_t)k].fe, xorshift(v->rng));
        } else if (d.mode[(size_t)k] == SAMPLE) {         // (SAMPLE: each note from the region's start - its end, REVERSE)
            const OpCoef& c = d.oc[(size_t)k];
            if (smp != nullptr) {
                double a = 0.0, b = 0.0;
                sampleRegion(*smp, c.b, c.fe, a, b);
                o.sp.start(c.ftype, a, b);
            } else {
                o.sp.done = true;
            }
        } else if (d.mode[(size_t)k] == MACRO) {          // (MACRO: the note strikes the model; a fresh voice from rest)
            Macro& mc = *macros[(size_t)((v - voices.data()) * 6 + k)];
            if (fresh) mc.reset();
            mc.strike();
        }
        o.strikeVel = v->vel / 127.0;                     // (MODAL: struck at the note's first control block; a voice
                                                          // stolen while ringing is struck again, as an object is)
        o.phase = patch.phase == 2 ? (xorshift(v->rng) >> 8) / 16777216.0 : ph0;
        o.gk = levelGain(po, v->vel, note, patch.lofi);
        o.vpit = 0.0;                                     // VEL PITCH
        if (po.velPitch && !po.fixed) {
            const auto& T = Tables::get();
            if (po.velCoarse) {
                const int st = (po.velPitch * 8 * v->vel + (po.velPitch < 0 ? -6350 : 6350)) / 12700;
                o.vpit = T.coarseLog2[(size_t)std::clamp(po.coarse + st, 0, kNumCoarse - 1)] - T.coarseLog2[(size_t)po.coarse];
            } else {
                o.vpit = po.velPitch * v->vel / 12700.0;   // up to an octave
            }
        }
        // RATE KEY (the chips' key scale rate): at 100 %, an octave above KT CENTER halves the stages' times, one below
        // doubles them (held within 1/32 .. 32)
        o.env.timeScale = po.rateKey > 0
                              ? std::clamp(std::exp2(-(note - (po.ktCenter ? po.ktCenter : 60)) / 12.0 * po.rateKey / 100.0),
                                           1.0 / 32.0, 32.0)
                              : 1.0;
        envGo(o.env, Env::Atk, opAdsr(*v, 5 - k));
    }
    liveAge = v->age;                                     // (the newest voice: the editor draws its sources)
    if (fresh) {                                          // the voice's filter from rest, EG1, EG2, ENV 1 from 0
        v->eg1.lvl = v->eg2.lvl = v->env1.lvl = 0.0;
        for (double& c : v->combSt) c = 0.0;
        for (bool& t : v->trigHigh) t = false;
        v->dc = 0.0;
        v->vf.reset();
        v->lpg.reset();
        v->flt = Slide {};
    }
    v->lpg.trigger();                                     // (PING: each note strikes the gate; EG2 mode ignores it)
    envGo(v->eg1, Env::Atk, eg1Adsr(*v));
    envGo(v->eg2, Env::Atk, eg2Adsr(*v));
    envGo(v->env1, Env::Atk, env1Adsr(*v));
    for (auto& f : v->func) f.start();                   // (FUNC: from its start at every note)
    for (int k = 0; k < kNumLfos; ++k) {                         // the LFOs: KEY SYNC (VOICE: its own phase; COMMON: the
        const auto& l = patch.lfo[k];                     // first key from none held), the FADE in
        const double reset = d.lfoReset[k] < 0.0 ? (xorshift(v->rng) >> 8) / 16777216.0 : d.lfoReset[k];
        if (l.keySync == 2) v->lph[k] = reset;
        else if (l.keySync == 1 && !held) lph[k] = reset;
        v->lfade[k] = l.keySync && l.fade ? 0.0 : 1.0;
    }
    if (d.cascOn) {                                       // SK CASC: the part's EG2 struck, this note the newest
        if (!part.live) {
            part.casc.reset();
            part.lp = part.hp = Slide {};
            part.vca = part.vcaStep = 0.0;
            part.ctlPos = 0;
            part.eg2.lvl = 0.0;
        }
        part.fade = 0;
        envGo(part.eg2, Env::Atk, patch.flt.eg2);
        part.note = note;
        part.live = true;
    }
}

void Engine::noteOff(int note, int channel)
{
    for (auto& v : voices)
        if (v.active && v.gate && v.note == note && (channel == -1 || v.channel == channel || (channel == -2 && v.channel == -1))) {
            if(v.channel>=0&&!v.expressionFrozen){v.releasePitch=channelPitchSmooth[v.channel];v.releasePressure=channelPressure[v.channel];v.releaseTimbre=channelTimbre[v.channel];v.expressionFrozen=true;}
            v.gate = false;
            for (int k = 0; k < 6; ++k) envRelease(v.op[(size_t)k].env, opAdsr(v, 5 - k));
            envRelease(v.eg1, eg1Adsr(v));
            envRelease(v.eg2, eg2Adsr(v));
            envRelease(v.env1, env1Adsr(v));
        }
}

// (every envelope let go, ADR's too: all notes off is a stop)
void Engine::allNotesOff(int channel)
{
    for (auto& v : voices)
        if (v.active && (channel < 0 || v.channel == channel)) {
            if(v.channel>=0&&!v.expressionFrozen){v.releasePitch=channelPitchSmooth[v.channel];v.releasePressure=channelPressure[v.channel];v.releaseTimbre=channelTimbre[v.channel];v.expressionFrozen=true;}
            v.gate = false;
            for (int k = 0; k < 6; ++k) envGo(v.op[(size_t)k].env, Env::Rel, patch.op[(size_t)(5 - k)].adsr());
            envGo(v.eg1, Env::Rel, patch.eg1);
            envGo(v.eg2, Env::Rel, patch.flt.eg2);
            envGo(v.env1, Env::Rel, patch.env1);
        }
}

int Engine::activeVoices() const
{
    int n = 0;
    for (const auto& v : voices) n += v.active ? 1 : 0;
    return n;
}

// one operator, one sample (opx_op_sample): in (cycles), its SELF, gain g; the output (cycles)
float Engine::opSample(Voice& v, int k, float in, float self, double g)
{
    const auto& T = Tables::get();
    Op& o = v.op[(size_t)k];
    const int mode = d.mode[(size_t)k], wave = d.wave[(size_t)k];
    const double ph = o.phase;
    float y = 0.0f;

    // the oscillator at phase p: a table (SAW HD / SQR HD: the two band-limited levels its octave lies between),
    // else a noise (one value a sample)
    const auto osc = [&](double p) -> float {
        if (!isNoiseWave(wave)) {
            if (wave == SAWHD || wave == SQRHD || (wave >= HALFSIN && wave <= LOGSAW)) {
                const auto& L = wave == SAWHD ? T.hdSaw : wave == SQRHD ? T.hdSqr : T.hdChip[(size_t)(wave - HALFSIN)];
                // the firmware picks by the increment at 44.1 kHz; here by the same harmonics-to-Nyquist relation
                // (the increment as the band's rate sees it: oversampled, the wave stays in the audible band)
                const double q = std::log2(std::max(o.inc * hdScale, 1e-12) * 16777216.0) - 15.0;
                const int n = (int)std::floor(q);
                int a = n + 1, c = n + 2;
                double w = q - n;
                if (a < 0) { a = c = 0; w = 0.0; }
                a = std::min(a, 8);
                c = std::min(c, 8);
                const float x = lookup(L[(size_t)a], p);
                return (float)(x + (lookup(L[(size_t)c], p) - x) * w);
            }
            return lookup(T.wave[(size_t)wave], p);
        }
        const float w = (float)((int32_t)(xorshift(v.rng) >> 16) - 32768) / 32768.0f;
        switch (wave) {
        case NSH: {                                       // a new value each cycle of the operator's pitch
            const double fr = ph - std::floor(ph);
            if (fr + o.inc >= 1.0) o.nz[0] = w;
            return o.nz[0];
        }
        case NPINK:                                       // three leaky poles (-3 dB an octave, roughly)
            o.nz[0] = o.nz[0] - o.nz[0] / 512.0f + w / 16.0f;
            o.nz[1] = o.nz[1] - o.nz[1] / 32.0f + w / 4.0f;
            o.nz[2] = o.nz[2] - o.nz[2] / 2.0f + w;
            return std::clamp((o.nz[0] + o.nz[1] + o.nz[2]) / 4.0f, -1.0f, 1.0f);
        case NBLUE: {                                     // white, differentiated
            const float b = (w - o.nz[1]) * 0.5f;
            o.nz[1] = w;
            return b;
        }
        case NBROWN: {                                    // a random walk, steps of 1/8, reflected at +-1
            float b = o.nz[0] + w * 0.125f;
            if (b > 1.0f) b = 2.0f - b;
            else if (b < -1.0f) b = -2.0f - b;
            o.nz[0] = b;
            return b;
        }
        case NGREY: {                                     // one random bit of a word flipped each sample
            o.grey ^= 1u << (xorshift(v.rng) >> 27);
            return (float)(int32_t)o.grey / 2147483648.0f;
        }
        case NCLIP: return w < 0.0f ? -1.0f : 1.0f;       // only the two ends
        case NRAMP: {                                     // from a value to the next over each cycle of the pitch
            const double fr = ph - std::floor(ph);
            const float y = o.nz[0] + (o.nz[1] - o.nz[0]) * (float)fr;
            if (fr + o.inc >= 1.0) {                      // (the cycle's last sample: the next starts at its target -
                o.nz[0] = o.nz[1];                        // advanced after the value, or the last sample jumps)
                o.nz[1] = w;
            }
            return y;
        }
        default: return w;
        }
    };

    if (mode != FM && self != 0.0f) in += o.y1 * self;
    const float m = std::clamp(in * 0.5f, -1.0f, 1.0f);  // the input as audio: a modulator at full = 1.0
    switch (mode) {
    case FM: {
        double p = ph + in;
        if (v.oc[(size_t)k].fbk != 0.0f) p += (o.y0 + o.y1) * v.oc[(size_t)k].fbk;
        if (self != 0.0f) p += (o.y0 + o.y1) * self * 0.5f;
        float s;
        if (v.oc[(size_t)k].winv != 0.0f) {                  // WIDTH: the wave over [0, width), silent after
            const double fr = p - std::floor(p);
            s = fr < v.oc[(size_t)k].wlim ? osc(fr * v.oc[(size_t)k].winv) : 0.0f;
        } else {
            s = osc(p);
        }
        y = (float)(s * g);
        break;
    }
    case RING: {
        const float s = osc(ph);
        float r = m + (std::fabs(m) - m) * v.oc[(size_t)k].b;  // SHAPE: rectified by this much
        r = s * r;
        y = (float)((s + (r - s) * v.oc[(size_t)k].a) * g);  // DEPTH
        break;
    }
    case FOLD: {
        const float s = osc(ph);
        const float x = (m + s * v.oc[(size_t)k].a) * v.oc[(size_t)k].b + v.oc[(size_t)k].bias;
        y = (float)(foldAA(x, o.foldX1) * g);
        break;
    }
    case BYPASS: y = in; break;
    case FILTER: {                                        // the input + OSCMIX x the oscillator through the filter
        const double x = m + osc(ph) * v.oc[(size_t)k].a;
        y = (float)(std::clamp(fstep(o.f, o.fc, opFilterType(v.oc[(size_t)k].ftype), x), -1.0, 1.0) * g);
        break;
    }
    case FILTFM: {                                        // the oscillator through it, its cutoff +-4 octaves by the input
        const FType& t = opFilterType(v.oc[(size_t)k].ftype);
        FCoef c;
        const CutoffRange& cr = fw2x ? kFw1x : kNative;
        const double cOff = v.oc[(size_t)k].foff - d.oc[(size_t)k].foff;   // (CUTOFF moved by a route)
        fset(c, t, std::exp2(o.lfk + fofs[(size_t)k] + cOff + 4.0 * m), v.oc[(size_t)k].fres, fs, cr.lo, cr.hi);
        y = (float)(std::clamp(fstep(o.f, c, t, osc(ph)), -1.0, 1.0) * g);
        break;
    }
    case MODAL: {                                         // the object (FmModal.h): the input + OSCMIX x the oscillator,
        const double ex = (m + osc(ph) * v.oc[(size_t)k].a) * o.eg;   // shaped by the envelope, excite it; the strike
        y = (float)(o.modal.process(ex, o.eg) * g);       // at a note is the bank's own; it rings out by itself
        break;
    }
    case CHAOS: {                                         // the generator, a step a cycle of the phase - FM moves it
        const OpCoef& c = v.oc[(size_t)k];
        y = (float)(o.chaos.process(ph + in, c.ftype, c.b, c.fe, c.fres) * g);
        break;
    }
    case MACRO: {                                         // the module's models (FmMacro.h), at the operator's pitch
        const OpCoef& c = v.oc[(size_t)k];
        Macro& mc = *macros[(size_t)((&v - voices.data()) * 6 + k)];
        const double hz = std::max(o.inc * fs, 1.0);
        const float note = (float)(69.0 + 12.0 * std::log2(hz / 440.0));
        y = (float)(mc.process(c.ftype, c.b / 100.0f, (float)(c.fe / 200.0), (float)(c.fres / 100.0), note,
                               v.vel / 127.0f, v.gate, in) * g);
        break;
    }
    case SAMPLE: {                                        // the recording at the operator's pitch (FmSample.h); the
        const OpCoef& c = v.oc[(size_t)k];                // input moves where it reads: phase modulation of the sound
        if (smp == nullptr) { y = 0.0f; break; }
        double a = 0.0, b = 0.0;
        sampleRegion(*smp, c.b, c.fe, a, b);
        const double perCycle = smp->rate / kSampleRootHz;   // (a cycle at the root, in the recording's samples)
        y = (float)(o.sp.process(*smp, c.ftype, a, b, c.fres, o.inc * perCycle, in * perCycle) * g);
        break;
    }
    case STOCH: {                                         // its breakpoints, a cycle of them a cycle of the phase
        const OpCoef& c = v.oc[(size_t)k];
        y = (float)(o.stoch.process(ph + in, c.ftype, c.b, c.fres) * g);
        break;
    }
    case EFFECT: {                                        // the input + OSCMIX x the oscillator through the effect
        const double x = m + osc(ph) * v.oc[(size_t)k].a;
        y = (float)(std::clamp(fx.step(o.fx, o.fxc, x), -1.0, 1.0) * g);
        break;
    }
    default: y = 0.0f; break;                             // MUTE
    }
    o.y0 = o.y1;
    o.y1 = y;
    o.phase = ph + o.inc;
    if (o.phase >= 1024.0) o.phase -= 1024.0;            // (keep the double's precision: whole cycles do not matter)
    return y;
}

void Engine::render(float* left, float* right, int n)
{
    smp = sampleSlot.take();                              // (SAMPLE: a new recording from this block on)
    // in chunks that end on the engine's control blocks (the common LFOs move once a block, as the firmware's
    // opx_block) and fit the part's buffer
    for (int done = 0; done < n;) {
        if (engPos == 0) engineBlock();
        const int m = std::min({ kChunk, n - done, ctl - engPos });
        renderChunk(left + done, right + done, m);
        done += m;
        engPos += m;
        if (engPos >= ctl) engPos = 0;
    }
}

void Engine::renderChunk(float* left, float* right, int n)
{
    performancePitchSmooth = performancePitch + (performancePitchSmooth - performancePitch) * std::exp(-n / (fs * .005));
    const double pitchDecay = std::exp(-n / (fs * .005));
    for(int c=0;c<16;++c) channelPitchSmooth[c]=channelPitch[c]+(channelPitchSmooth[c]-channelPitch[c])*pitchDecay;
    // the operators' CUTOFF / FREQ slide: 1/32 of the way a control block (the firmware's OPX_VFLT_SLIDE)
    const double sl = fofsOk ? 1.0 - std::pow(31.0 / 32.0, (double)n / ctl) : 1.0;
    for (int k = 0; k < 6; ++k) fofs[(size_t)k] += (d.oc[(size_t)k].foff - fofs[(size_t)k]) * sl;
    fofsOk = true;
    const auto& vfp = patch.flt;
    const CutoffRange opRange = fw2x ? kFw1x : kNative;  // (the operators' filters: the firmware's at 1x, or open)
    if (d.cascOn) std::fill(partBuf.begin(), partBuf.begin() + n, 0.0f);
    bool anyGate = false, anySounding = false;
    for (auto& v : voices) {
        if (!v.active) continue;
        const double lf = std::log2(440.0) + (v.note - 69) / 12.0
            + performancePitchSmooth
            + (v.channel >= 0 ? (v.expressionFrozen ? v.releasePitch : channelPitchSmooth[v.channel]) : 0.0)
            ;
        float y[6];
        for (int k = 0; k < 6; ++k) y[k] = v.op[(size_t)k].y1;
        for (int i = 0; i < n; ++i) {
            double sum = 0.0;
            if (v.ctlPos == 0) {                          // a new control block (opx_render's setup)
                // the LFOs: the voice's own phase (KEY SYNC VOICE) or the common one, faded in
                // (the voice's routes, from its last block, move SPEED, SYMMETRY, MORPH and FADE: an LFO by another)
                for (int k = 0; k < kNumLfos; ++k) {
                    const auto& l = patch.lfo[k];
                    const int xb = XV_L1SYNC + k * kLfoXValues;
                    double ph = lph[k];
                    if (l.keySync == 2) {
                        ph = v.lph[k];
                        v.lph[k] += d.lfoCyc[k] * (l.sync > 0 ? 1.0 : lfoSpeedMul(v, k));
                        v.lph[k] -= std::floor(v.lph[k]);
                    }
                    double val = lfoValue(k, ph, v.mod[(size_t)extIndex(xb + 1)], v.mod[(size_t)extIndex(xb + 3)]);
                    if (v.lfade[k] < 1.0) {
                        const int fi = k == 0 ? globalIndex(GV_L1FADE) : k == 1 ? globalIndex(GV_L2FADE) : extIndex(XV_L3FADE);
                        double step = d.lfoFadeStep[k];
                        if (v.mod[(size_t)fi] != 0.0) {  // (FADE moved: its time index, and the step a block it makes)
                            const int idx = std::clamp((int)std::lround(l.fade + v.mod[(size_t)fi]), 0, 127);
                            const double ms = Tables::get().envMs[(size_t)idx];
                            step = ms > 0.0 ? 1.0 / std::max(1.0, std::round(ms / 1000.0 * fs / ctl)) : 1.0;
                        }
                        v.lfade[k] = std::min(1.0, v.lfade[k] + step);
                        val *= v.lfade[k];
                    }
                    v.lfo[k] = val;
                }
                const double e1 = envTick(v.eg1, eg1Adsr(v)), e2 = envTick(v.eg2, eg2Adsr(v));
                const double en1 = envTick(v.env1, env1Adsr(v));
                // the sources and the routes: this voice's offsets on its destinations (no route: all 0, the patch
                // as it is; the sources still run, for the editor's live drawings)
                modBlock(v, en1, e1, e2);
                const auto mo = [&](int idx) { return v.mod[(size_t)idx]; };
                // the operators: their mode's settings (moved by the routes on A..D), their pitch (EG1, LFO1, the
                // routes on FINE, TRANS, DETUNE) and what follows it (FILTER's coefficients, EFFECT's settings),
                // their envelopes and the gains' ramps (LEVEL moved; LFO1 -> level: x (1 + depth x LFO1))
                for (int k = 0; k < 6; ++k) {
                    Op& o = v.op[(size_t)k];
                    const int op = 5 - k;
                    const OpParams& po = patch.op[(size_t)op];
                    const double ma = mo(opIndex(op, OV_A)), mb = mo(opIndex(op, OV_B)), mc = mo(opIndex(op, OV_C)),
                                 md = mo(opIndex(op, OV_D));
                    v.oc[(size_t)k] = ma != 0.0 || mb != 0.0 || mc != 0.0 || md != 0.0
                                          ? opCoef(po, patch.fbOffset, ma, mb, mc, md) : d.oc[(size_t)k];
                    const double pf = pitchMod(e1, po.eg1 + mo(opIndex(op, OV_EG1)), v.lfo[0],
                                               po.lfo1Pitch + mo(opIndex(op, OV_L1P)));
                    double pm = 0.0;                      // (a ratio's: a fixed frequency has no FINE, TRANS, DETUNE)
                    if (!po.fixed) {
                        const double mf = mo(opIndex(op, OV_FINE));
                        if (mf != 0.0) pm += std::log2(std::max(po.fine + mf, 1.0) / po.fine);
                        pm += mo(opIndex(op, OV_TRANS)) / 12.0 + mo(opIndex(op, OV_DETUNE)) / 1200.0;
                    }
                    o.lfk = d.fixed[(size_t)k] ? d.baseLog2[(size_t)k] + pf : lf + d.baseLog2[(size_t)k] + o.vpit + pf + pm;
                    o.inc = std::exp2(o.lfk) / fs;
                    const int mode = d.mode[(size_t)k];
                    const double cOff = v.oc[(size_t)k].foff - d.oc[(size_t)k].foff;   // (C moved: on the slid one)
                    if (mode == FILTER)
                        fset(o.fc, opFilterType(v.oc[(size_t)k].ftype), std::exp2(o.lfk + fofs[(size_t)k] + cOff),
                             v.oc[(size_t)k].fres, fs, opRange.lo, opRange.hi);
                    else if (mode == EFFECT)
                        fx.setup(o.fxc, v.oc[(size_t)k].ftype, v.oc[(size_t)k].fe, v.oc[(size_t)k].fres,
                                 std::exp2(o.lfk + fofs[(size_t)k] + cOff), o.inc);
                    if (mode == MODAL) {                  // the object: its modes now; a new note's strike
                        const ModalSettings ms = modalSettings(v, k);
                        // (the bank at 2 x the host's rate: oversampled N x, a step every N / 2 samples)
                        o.modal.update(ms, fs, 20000.0, op, std::max(1, (int)std::lround(hdScale / 2.0)));
                        if (o.strikeVel >= 0.0) o.modal.strikeNote(ms, fs, o.strikeVel);
                    }
                    o.strikeVel = -1.0;
                    o.gk = levelGain(po, v.vel, v.note, patch.lofi, mo(opIndex(op, OV_LEVEL)));
                    double env = envTick(o.env, opAdsr(v, op));
                    if (patch.lofi) env = std::floor(env * 64.0) / 64.0;   // LOFI: the envelope in 64 steps
                    // (MODAL: the envelope shapes what goes into the object, not its ring: the level alone goes out)
                    double target = o.gk * (mode == MODAL ? 1.0 : env);
                    const double l1l = d.l1Level[(size_t)k] + mo(opIndex(op, OV_L1L)) / 100.0;
                    if (l1l != 0.0) target *= std::max(0.0, 1.0 + v.lfo[0] * l1l);
                    o.g = patch.lofi ? target : o.gEnd;  // (LOFI: a step a block, no ramp)
                    o.dg = patch.lofi ? 0.0 : (target - o.gEnd) / ctl;
                    o.gEnd = target;
                    const double et = mode == MODAL ? env : 0.0;
                    o.eg = o.egEnd;
                    o.deg = (et - o.egEnd) / ctl;
                    o.egEnd = et;
                }
                // the voice filter's cutoff: slid (the patch's), plus EG2, LFO2, the key track and the routes (on the
                // moving part: a route is not slowed), ramped; its RESO, DRIVE and OUT GAIN as the routes move them
                const auto mg = [&](int g) { return mo(globalIndex(g)); };
                if (d.filterOn)
                    v.flt.block(d.flg, pitchMod(e2, vfp.eg2Int + mg(GV_FEG2), v.lfo[1], vfp.lfo2Int + mg(GV_FLFO2)) +
                                           keyTrack(v.note) + mg(GV_FCUT) * cutSpan() / 1000.0, ctl);
                v.fReso = std::clamp(vfp.reso + mg(GV_FRES), 0.0, 100.0);
                v.fDrive = mg(GV_FDRIVE) != 0.0 ? std::exp2(3.0 * std::clamp(vfp.drive + mg(GV_FDRIVE), 0.0, 100.0) / 100.0)
                                                : d.drive;
                v.outGain = mg(GV_OGAIN) != 0.0 ? d.outGain * std::pow(10.0, 0.75 * mg(GV_OGAIN) / 20.0) : d.outGain;
                v.lpgDecay = (float)(std::clamp(patch.lpgDecay + mo(extIndex(XV_LPGDECAY)), 0.0, 100.0) / 100.0);
                v.lpgColor = (float)(std::clamp(patch.lpgColor + mo(extIndex(XV_LPGCOLOR)), 0.0, 100.0) / 100.0);
            }
            if (++v.ctlPos >= ctl) v.ctlPos = 0;
            for (int k = 0; k < 6; ++k) {
                Op& o = v.op[(size_t)k];
                o.g += o.dg;
                o.eg += o.deg;
                const double g = o.g;
                // below the firmware's threshold on both ends of the block (msfa kLevelThresh): silent
                const bool quiet = o.g < 6.7e-5 && o.gEnd < 6.7e-5;
                if (d.mode[(size_t)k] == MUTE || (quiet && d.mode[(size_t)k] != BYPASS)) {
                    y[k] = 0.0f;
                    o.y0 = o.y1;
                    o.y1 = 0.0f;
                    o.phase += o.inc;
                    if (o.phase >= 1024.0) o.phase -= 1024.0;
                    continue;
                }
                float in = 0.0f;
                for (int j = 0; j < 6; ++j)
                    if (j != k && d.mx[(size_t)j][(size_t)k] != 0.0f) in += y[j] * d.mx[(size_t)j][(size_t)k];
                y[k] = opSample(v, k, in, d.mx[(size_t)k][(size_t)k], g);
                if ((d.dout >> k) & 1) sum += y[k];
            }
            // the voice filter; LOFI's 12 bits; the voice's DC blocker (after the filter, as the firmware's); then
            // the output (OUT GAIN), or SK CASC's sum (the part's filter)
            double yv = sum;
            if (d.filterOn) yv = v.vf.process(yv, v.flt.next(), vfp.type, v.fReso, v.fDrive, vfp.cmosLife);
            if (patch.lpgMode != VoiceLpg::kOff)          // the LPG, after the filter (the gate and the VCA in one)
                yv = v.lpg.process((float)yv, patch.lpgMode, v.lpgDecay, v.lpgColor, (float)v.eg2.lvl, (float)v.note);
            if (patch.lofi) yv = std::floor((yv + 1.0 / 4096.0) * 2048.0) / 2048.0;
            const double x = yv - v.dc;
            v.dc += x * dcCoef;
            if (d.cascOn) {
                partBuf[(size_t)i] += (float)x;
            } else {
                const float o = (float)(x * kOut * v.outGain);
                left[i] += o;
                right[i] += o;
            }
        }
        // the voice ends when every heard operator's envelope is done (a BYPASS carrier: every operator heard);
        // SK CASC with its VCA on EG2: also when the part's EG2 has ended after the note was let go
        unsigned need = d.car;
        for (int k = 0; k < 6; ++k)
            if (((d.car >> k) & 1) && d.mode[(size_t)k] == BYPASS) need = 0x3F;
        bool live = false;
        for (int k = 0; k < 6; ++k) {
            if (!((need >> k) & 1) || d.mode[(size_t)k] == MUTE) continue;
            const Env& e = v.op[(size_t)k].env;
            if (!(e.st == Env::Off || (e.st == Env::Sus && e.lvl <= 0.0))) live = true;
            if (d.mode[(size_t)k] == MODAL && v.op[(size_t)k].modal.level > 3e-5) live = true;   // (still ringing)
        }
        if (d.cascOn && vfp.cascVca == 1 && !v.gate && part.eg2.st == Env::Off) live = false;
        if (patch.lpgMode == VoiceLpg::kPing && v.lpg.closed()) live = false;   // (PING: the gate fell shut)
        if (!live) v.active = false;
        anyGate = anyGate || (v.active && v.gate);
        anySounding = anySounding || v.active;
    }
    if (d.cascOn && part.live) renderPart(left, right, n, anyGate, anySounding);
}

// SK CASC (eng_opx.c opx_post): the voices' sum through the part's filter, its VCA, CASC OUT + OUT GAIN. The sum
// enters at the firmware's level (a voice's amplitude: VOICE_FS / 32768 of the bus) times FLT DRIVE, and goes back
// out by the same; once no voice sounds it fades over a control block and the filter rests
void Engine::renderPart(float* left, float* right, int n, bool anyGate, bool anySounding)
{
    constexpr double kIn = 24000.0 / 32768.0;
    const auto& f = patch.flt;
    if (!anySounding && part.fade == 0) part.fade = ctl;   // (no voice left: the fade begins)
    for (int i = 0; i < n; ++i) {
        if (part.ctlPos == 0) {
            if (!anyGate && part.eg2.st != Env::Rel && part.eg2.st != Env::Off)
                envRelease(part.eg2, f.eg2);              // the last note let go (ADR: it goes on by itself)
            const double e2 = envTick(part.eg2, f.eg2), ktm = keyTrack(part.note);
            const double l2 = lfoValue(1, lph[1]);       // (the part's LFO2: the common one)
            // the routes: one filter for every voice, moved as the newest voice's routes move it
            const Voice* nv = nullptr;
            for (const auto& v : voices)
                if (v.active && v.age == liveAge) nv = &v;
            const auto mg = [&](int g) { return nv ? nv->mod[(size_t)globalIndex(g)] : 0.0; };
            part.lp.block(d.flg, pitchMod(e2, f.eg2Int + mg(GV_FEG2), l2, f.lfo2Int + mg(GV_FLFO2)) + ktm +
                                     mg(GV_FCUT) * cutSpan() / 1000.0, ctl);
            part.hp.block(d.hlg, pitchMod(e2, f.hpEg2Int, l2, f.hpLfo2Int) + ktm + mg(GV_HPCUT) * cutSpan() / 1000.0, ctl);
            part.reso = std::clamp(f.reso + mg(GV_FRES), 0.0, 100.0);
            part.hpPeak = std::clamp(f.hpPeak + mg(GV_HPPEAK), 0.0, 100.0);
            part.fb = std::clamp(f.cascFb + mg(GV_CFB), -100.0, 100.0) * 2.0 / 100.0;
            part.drive = mg(GV_FDRIVE) != 0.0 ? std::exp2(3.0 * std::clamp(f.drive + mg(GV_FDRIVE), 0.0, 100.0) / 100.0)
                                              : d.drive;
            part.gain = mg(GV_OGAIN) != 0.0 ? d.cascGain * std::pow(10.0, 0.75 * mg(GV_OGAIN) / 20.0) : d.cascGain;
            double g1 = 1.0;
            if (f.cascVca == 1) {
                g1 = std::max(e2, 0.0);                    // EG2
            } else if (f.cascVca == 2) {                   // ENV: the loudest carrier's envelope
                g1 = 0.0;
                for (const auto& v : voices)
                    if (v.active)
                        for (int k = 0; k < 6; ++k)
                            if ((d.car >> k) & 1) g1 = std::max(g1, v.op[(size_t)k].env.lvl);
                g1 = std::min(g1, 1.0);
            }
            part.vcaStep = f.cascVca ? (g1 - part.vca) / ctl : 0.0;
            if (!f.cascVca) part.vca = 1.0;
        }
        if (++part.ctlPos >= ctl) part.ctlPos = 0;
        const double x = partBuf[(size_t)i] * kIn * part.drive;
        double y = part.casc.process(x, part.hp.next(), part.lp.next(), part.hpPeak, part.reso, part.fb, f.cascHot);
        part.vca += part.vcaStep;
        y *= part.vca * part.gain / kIn;
        if (part.fade > 0) {                               // (no voice: down to 0 over the block, then rest)
            y *= (double)(part.fade - 1) / ctl;
            if (--part.fade == 0) {
                part.live = false;
                part.casc.reset();
                left[i] += (float)(y * kOut);
                right[i] += (float)(y * kOut);
                return;
            }
        }
        left[i] += (float)(y * kOut);
        right[i] += (float)(y * kOut);
    }
}

} // namespace usina::fm
