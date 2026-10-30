// SPDX-License-Identifier: GPL-3.0-only
// Usina FM engine: six operators a voice (the waves, the noises, the level and its envelope, velocity, key tracking,
// VEL PITCH), the modes FM (feedback, WIDTH), RING, FILTER, FILTER FM, FOLD, EFFECT (17 of them: FmEffects.h),
// BYPASS and MUTE, the 40 algorithms, ALG FB and the user matrix; after them the voice filter (18 types, EG2, key
// track, drive; SK CASC the part's, with its VCA) and OUT GAIN, LOFI. Native: floating point, at the rate it is
// prepared at (the plugin prepares it at N x the host's: everything in the voice oversampled, one decimation after),
// every operator one sample at a time through the matrix; envelopes update per control block.
// Per-instance state with no globals. Reference-format presets retain their
// parameter names and ordering.
#pragma once
#include "FmEffects.h"
#include "FmFilters.h"
#include "FmMod.h"
#include "FmModal.h"
#include "FmUnison.h"
#include "FmModSources.h"
#include "FmPatch.h"
#include <atomic>
#include "FmTables.h"
#include "FmVoiceFilter.h"
#include "FmLpg.h"
#include "FmChaos.h"
#include "FmMacroModels.h"
#include "FmSample.h"
#include <memory>
#include <array>
#include <cstdint>
#include <vector>

namespace usina::fm {

class Macro;                                            // (FmMacro.h: the MACRO operator's models)
struct MacroDelete { void operator()(Macro* m) const noexcept; };

class Engine {
public:
    static constexpr int kMaxVoices = 16;
    static constexpr int kNumLfos = 3;           // LFO1, LFO2 (the firmware's), LFO3 (ours)

    // rate: the engine's own (the host's x the oversampling); firmware2x: the voice filter the firmware's way (its
    // saturating types and CMOS at 2x inside: the A/B at 1x); else everything at the rate
    // band: the rate whose band the band-limited waves fill (the host's: oversampled, the waves stay in the audible
    // band and the headroom above is left to the folds, shapers and saturations; 0 = the engine's rate)
    void prepare(double rate, bool firmware2x = false, double band = 0.0);
    void setPatch(const Patch& p);               // takes effect for the next notes and the held ones' settings
    void noteOn(int note, int velocity, int channel = -1);
    void noteOff(int note, int channel = -1); // -1: every channel; -2: legacy keyboard only
    void allNotesOff(int channel = -1);
    // adds the voices into out (stereo; mono for now, the same in both), n samples
    void render(float* left, float* right, int n);
    // SAMPLE (FmSample.h): the recording the SAMPLE operators play - given on the message thread (nullptr: none),
    // taken by the audio thread at its next block; collectSample frees the one it let go (message thread, any time)
    void setSample(std::unique_ptr<SampleData> s) { sampleSlot.give(std::move(s)); }
    void collectSample() { sampleSlot.collect(); }
    int activeVoices() const;
    // the links of a preset algorithm (0..39) as the engine makes them: links[from][to] in OP order (OP1 = 0), 1.0
    // a link, ALG FB's amount on its link; carriers: bit k = OP(k + 1) is heard
    static void algorithmLinks(int alg, int algFb, float links[6][6], unsigned& carriers);

    // the modulation (FmMod.h), all on the audio thread: the routes (a copy: the processor hands them over), the
    // keyboard's controllers (0..1) and the four macros (-1..1) as sources
    void setRoutes(const Routes& r);
    void setControllers(double aftertouch, double modWheel, double expression);
    // Performance controls are separate from the sound patch. -1 is the legacy shared keyboard.
    void setPerformancePitch(double octaves) { performancePitch = octaves; }
    void setChannelPitch(int channel, double octaves) { if(channel>=0&&channel<16) channelPitch[channel]=octaves; }
    void setChannelPressure(int channel, double value) { if(channel>=0&&channel<16) channelPressure[channel]=value; }
    void setChannelTimbre(int channel, double value) { if(channel>=0&&channel<16) channelTimbre[channel]=value; }
    void setTimbre(double value) { timbre = value; }
    void setMacroSources(const float m[4]);
    void setSeqSources(const float s[4]);               // the sequencer's modulation lanes (0..1: SEQ 1..4)
    void setCtrlSources(const float s[8]);              // the controllers (0..1: CTRL 1..8, a CC or a note's velocity)
    void setFunctionShape(int k, const FuncShape& s);   // FUNC k's points (the session's; the processor hands them)
    void setInputLevel(double level);                   // the audio input's level 0..1 (COMB INPUT LEVEL)
    // the host's transport, at the start of each render call: the tempo (SYNC's divisions), where it is in quarter
    // notes and whether it plays (a free LFO with SYNC then locks its phase to it: rule 10, ppqPosition)
    void setTransport(double bpm, double ppq, bool playing);
    // a source's value as the newest sounding voice has it now (for the editor's live drawings; any thread)
    float liveSource(int src) const { return src > 0 && src < kNumSources ? live[(size_t)src].load(std::memory_order_relaxed) : 0.0f; }
    // an LFO wave from rest over `cycles` cycles, `per` values a cycle (the tests' view of the random and chaotic
    // waves' motion: their per-cycle steps as the engine takes them)
    static std::vector<double> lfoTrace(int wave, int cycles, int per);

private:
    double performancePitch = 0.0, performancePitchSmooth = 0.0;
    std::array<double,16> channelPitch {}, channelPitchSmooth {}, channelPressure {};
    std::array<double,16> channelTimbre { .5,.5,.5,.5,.5,.5,.5,.5,.5,.5,.5,.5,.5,.5,.5,.5 };
    double timbre = .5;
    struct Env {
        enum Stage { Off, Atk, Dec, Sus, Rel } st = Off;
        double lvl = 0.0, step = 0.0, target = 0.0, k = 0.0;
        int64_t n = 0;
        int curve = 0;
        int64_t n0 = 0;                          // (M4: a curved attack: its length, where it started, its curve)
        double start = 0.0;
        int atkCurve = 0;
        double timeScale = 1.0;                  // (RATE KEY: the stages' times by the note - an operator's)
    };
    // a cutoff, smoothing (opx_vflt_lg): the patch's part slides (two one-poles, 1/16 a control block: ~23 ms), the
    // moving part (EG2, key track) adds on at once, the sum ramps across the block from where the last one ended
    struct Slide {
        double s0 = 0.0, s1 = 0.0, pv = 0.0, cur = 0.0, step = 0.0;
        bool fresh = true;
        void block(double target, double dyn, int ctl);
        double next() { return cur += step; }
    };
    // an operator's mode settings (from A, B, C, D, as eng_opx.c opx_sync makes them): the patch's in Derived, a
    // voice's own in Voice (the routes move them continuously)
    struct OpCoef {
        float fbk = 0, wlim = 0, winv = 0;           // FM: feedback; WIDTH (0: off)
        float a = 0, b = 0, bias = 0;                // RING: DEPTH, SHAPE; FOLD: OSCMIX, GAIN, BIAS; FILTER, EFFECT: a = OSCMIX
        int ftype = 0;                               // FILTER: the type; EFFECT: the effect
        double fres = 0, fe = 100, foff = 0;         // RESO / F; CUTOFF / E (0..200); CUTOFF / FREQ above the pitch, octaves
    };
    struct Op {
        Env env;
        double phase = 0.0, inc = 0.0;           // cycles; cycles a sample
        float y0 = 0.0f, y1 = 0.0f;              // its last two outputs (cycles of deviation: 2.0 = full)
        double gk = 0.0;                         // the level's gain (2.0 at LEVEL 100), with velocity and key track
        double g = 0.0, dg = 0.0, gEnd = 0.0;    // the gain now, its step a sample, its value at the block's end
        float nz[3] {};                          // the noises' states
        uint32_t grey = 0;                       // (GREY's word)
        float foldX1 = 0.0f;                     // FOLD: the last input (antiderivative antialiasing)
        double vpit = 0.0;                       // VEL PITCH: a log2 offset
        double lfk = 0.0;                        // its frequency, log2 Hz (FILTER's cutoff and EFFECT's FREQ follow)
        FState f;                                // FILTER, FILTER FM: the filter's states; fc its coefficients
        FCoef fc;                                // (FILTER: once a control block)
        FxState fx;                              // EFFECT: the effect's states and line; fxc its settings
        FxCoef fxc;
        ModalBank modal;                         // MODAL: the object's modes
        Chaos chaos;                             // CHAOS: its generator (FmChaos.h)
        Stochastic stoch;                        // STOCHASTIC: its breakpoints (FmChaos.h)
        double eg = 0.0, deg = 0.0, egEnd = 0.0; // MODAL: the envelope on the excitation (its ramp, as g's)
        double strikeVel = -1.0;                 // MODAL: a note's strike, struck at its first control block (0..1)
        SamplePlay sp;                           // SAMPLE: where this voice reads the recording
        UnisonState uni;                         // UNISON: the side copies' phases and drifts (FmUnison.h)
        float side = 0.0f;                       // stereo, stage 2: its side (right minus left, halved; 0: mono)
    };
    struct Voice {
        bool active = false, gate = false;
        int note = 0, vel = 0, channel = -1;
        bool expressionFrozen = false;
        double releasePitch = 0, releasePressure = 0, releaseTimbre = .5;
        uint64_t age = 0;
        std::array<Op, 6> op {};                 // the CORE order: index k = OP(6 - k), as the firmware's
        uint32_t rng = 0x6D2B79F5u;
        double dc = 0.0;
        int ctlPos = 0;                          // samples into the control block
        Env eg2;                                 // EG2: the filter's envelope
        VoiceFilter vf;
        VoiceLpg lpg;                            // the low pass gate (FmLpg.h), after the filter
        VoiceFilter vfR;                         // stereo, stage 2: the right channel's filter, LPG and DC blocker
        VoiceLpg lpgR;
        double dcR = 0.0, fltOff = 0.0;          // (fltOff: OFFSET this block, octaves)
        float lpgDecay = 0.5f, lpgColor = 0.5f;  // (its DECAY and COLOR as the routes move them, 0..1)
        float lpgDecayR = 0.5f;                  // (the LPG in stereo: the right's DECAY; the sources' levels,
        float lpgSrcL = 0.0f, lpgSrcR = 0.0f;    // 0..1 - a bipolar source from -1..1 to 0..1)
        Slide flt;                               // the filter's cutoff (log2 Hz): slid and ramped
        VoiceFilter vf2, vf2R;                   // second filter: left (or mono) and right
        Slide flt2;                              // second filter cutoff, log2 Hz
        double f2Reso = 0.0, f2Drive = 1.0, f2Mix = 1.0;   // per control block, modulation applied
        Env eg1;                                 // EG1: the operators' pitch envelope
        double lph[kNumLfos] {}, lfade[kNumLfos] { 1.0, 1.0, 1.0 };   // the LFOs' own phases (KEY SYNC VOICE), fades
        double lfo[kNumLfos] {};                 // the LFOs' values this control block
        Env env1;                                // ENV 1 (the modulation's own envelope)
        std::array<OpCoef, 6> oc {};             // the operators' mode settings this block (the patch's, or moved)
        std::array<double, kNumValues> mod {};   // the routes' offsets this block, in each value's own units
        double fReso = 0.0, fDrive = 1.0, outGain = 1.0;   // the voice filter's RESO, DRIVE, OUT GAIN this block
        // the voices in stereo: where the note sits (-1..1, from SPREAD at its start), its two gains now and
        // their step a sample (ramped across a control block; at the middle both exactly 1: mono as before)
        double place = 0.0, panL = 1.0, panR = 1.0, dPanL = 0.0, dPanR = 0.0;
        std::array<FuncState, 3> func {};        // FUNC 1..3, run per note
        double combSt[3] {};                     // COMB 1..3's state (LAG, FOLLOWER)
        bool trigHigh[kNumLfos + kNumEnvs + 3] {};   // RETRIG: each LFO's, envelope's, FUNC's source above 0.5 last block
    };
    // an LFO's random values (opx_lrnd_t): S/H's this cycle's and the last one's; the random-in-time waves' pulse
    // (+-1, flipped or not, a coin a cycle) and level (renewed or not), each with its last cycle's
    struct LfoRandom {
        double held = 0, prev = 0, tp = 0, tpp = 0, lv = 0, lvp = 0;
        uint32_t rnd = 0;
        // the chaotic waves' maps, a step a cycle: LOGISTIC (r 3.9), HENON (1.4, 0.3), LORENZ (the classic
        // flow, 8 steps of 0.01 a cycle); each one's value now and its last cycle's (-1..1), the LFO gliding between
        double logX = 0.3, henX = 0.1, henY = 0.1, lorX = 1.0, lorY = 1.0, lorZ = 1.0;
        double logV = 0, logP = 0, henV = 0, henP = 0, lorV = 0, lorP = 0;
    };
    static constexpr int kNumLfoWaves = 26;      // the firmware's 23, then CHAOS LOGISTIC, HENON, LORENZ
    // SK CASC's part: one filter after the voices (their sum), EG2 struck by every note, its key track the newest's
    struct Part {
        bool live = false;
        Env eg2;
        int note = 60;
        Casc casc;
        Slide lp, hp;
        double vca = 0.0, vcaStep = 0.0;
        int ctlPos = 0, fade = 0;
        double reso = 0.0, hpPeak = 0.0, fb = 0.0, drive = 1.0, gain = 1.0;   // this block's (the routes move them)
    };
    // the settings a sample needs, derived from the patch (core order)
    struct Derived {
        std::array<double, 6> baseLog2 {};      // ratio: log2 offset from the note; fixed: log2 Hz
        std::array<bool, 6> fixed {};
        std::array<OpCoef, 6> oc {};                 // the operators' mode settings, as the patch has them
        std::array<int, 6> mode {}, wave {};
        std::array<std::array<float, 6>, 6> mx {};   // [from][to], core order (1.0 = a preset algorithm's link)
        uint8_t dout = 0, car = 0;
        bool filterOn = false, cascOn = false;       // the voice filter (not CASC); SK CASC (the part's)
        bool stereo = false;                         // stage 2: a voice path in two channels (else mono, as before)
        int f2Route = 0, f2Type = 0;                 // f2Type is a voice filter index (SK CASC skipped)
        double f2lg = 0.0;                           // second filter cutoff, log2 Hz
        double flg = 0.0, hlg = 0.0;                 // CUTOFF, HP CUT as log2 Hz
        double drive = 1.0, cascFb = 0.0;            // FLT DRIVE x1..x8; CASC FB x -2..2
        double outGain = 1.0, cascGain = 1.0;        // OUT GAIN (a voice's), CASC OUT + OUT GAIN (the part's)
        int altSide = 1;                             // (SPREAD ALTERNATE: the side the next note takes)
        std::array<double, 6> l1Level {};            // LFO1 -> level (-1..1)
        double lfoCyc[kNumLfos] {}, lfoFadeStep[kNumLfos] {};      // the LFOs: cycles a control block; their fades' step a block
        double lfoReset[kNumLfos] {};                       // the phase a key restarts them at (< 0: random)
    };

    void derive();
    void lfoRates();                                 // the LFOs' cycles a block (SPEED, or SYNC at the host's tempo)
    void updateStereo();                             // stage 2: whether the voice path is two channels (patch, routes)
    double cascSrcLevel = 0.0;                       // SK CASC's VCA SOURCE: the newest voice's source level, 0..1
    void envGo(Env& e, Env::Stage st, const Adsr& a) const;
    double envTick(Env& e, const Adsr& a) const;
    void envRelease(Env& e, const Adsr& a) const;    // the key let go (ADR: ignored, it releases by itself)
    double lfoValue(int k, double ph) const;         // LFO k at phase ph: SYMMETRY, MORPH, POLARITY applied
    double keyTrack(int note) const;                 // the filter's key track, octaves
    static double lfoWave(int wave, double ph, const LfoRandom& r);   // an LFO's value at phase ph (cycles)
    static void lfoCycle(LfoRandom& r, uint32_t& rnd);                // a new cycle: the random ones move on
    void engineBlock();                              // once a control block: the common LFOs advance
    double pitchMod(double env, double envDepth, double lfo, double lfoDepth) const;   // (1/16 semitone depths) -> octaves
    // an operator's mode settings from its values plus the routes' offsets to A..D (0: exactly the firmware's)
    static OpCoef opCoef(const OpParams& o, int fbOffset, double da, double db, double dc, double dd);
    // the routes, for one voice this control block: its sources' values, the offsets on its destinations
    void modBlock(Voice& v, double env1, double env2, double env3);
    void applyRoutes(Voice& v, const double* s);    // the routes' offsets from the sources' values s
    void noteOnRoutes(Voice& v);                     // a note's: from the sources it has at once (velocity, key ...)
    // a MODAL operator's settings this block (core index k, OP(6 - k)): its values plus the voice's routes
    ModalSettings modalSettings(const Voice& v, int k) const;
    // an envelope as the voice's routes move it: its ATK, DEC, SUS, REL, CURVE (five values from index i0) and ATK
    // CURVE (index iAc); no route on them: the patch's own, untouched
    Adsr movedAdsr(const Voice& v, const Adsr& a, int i0, int iAc) const;
    Adsr opAdsr(const Voice& v, int op) const;       // OP(op + 1)'s, moved
    Adsr eg1Adsr(const Voice& v) const;
    Adsr eg2Adsr(const Voice& v) const;
    Adsr env1Adsr(const Voice& v) const;
    // an LFO's speed moved by the voice's routes (x the patch's rate), its SYMMETRY and MORPH offsets
    double lfoSpeedMul(const Voice& v, int k) const;
    double lfoValue(int k, double ph, double dSym, double dMorph) const;
    void renderChunk(float* left, float* right, int n);
    void renderPart(float* left, float* right, int n, bool anyGate, bool anySounding);
    float opSample(Voice& v, int k, float in, float self, double g);
    static uint32_t xorshift(uint32_t& s);

    double fs = 48000.0;
    double hdScale = 1.0;                            // the engine's rate / the band's (SAW HD, SQR HD: their level)
    bool fw2x = false;
    double dcCoef = 0.0;
    Part part;
    std::vector<float> partBuf;                      // the voices' sum for SK CASC (sized in prepare())
    // the common LFOs (KEY SYNC OFF and COMMON; their random values serve every voice): the phase in cycles, the
    // random state, where the engine's control block is
    double lph[kNumLfos] {};
    LfoRandom lr[kNumLfos];
    // the modulation: the routes, the controllers and the macros as sources, the newest voice's sources (live)
    Routes routes;
    double ctlAt = 0.0, ctlMw = 0.0, ctlExpr = 0.0;
    float macroSrc[4] {};
    float seqSrc[4] {};
    float ctrlSrc[8] {};
    std::array<std::atomic<float>, kNumSources> live {};
    uint64_t liveAge = 0;
    std::array<FuncShape, 3> funcShapes { defaultFuncShape(0), defaultFuncShape(1), defaultFuncShape(2) };
    std::array<RandGen, 3> rands {};
    double randVal[3] {};
    double inputLevel = 0.0;
    // the engine-wide sources' settings as the newest voice's routes move them: the common LFOs' rate (x), RAND k's
    // SPEED, A, B, RISE, FALL (offsets)
    double lfoRateMul[kNumLfos] { 1.0, 1.0, 1.0 };
    double randMod[3][5] {};
    bool randTrigHigh[3] {};                         // RAND k's RETRIG source above 0.5 last block (the newest voice's)
    double tpBpm = 120.0, tpPpq = 0.0;                // the transport (tpPpq: at the next control block)
    bool tpPlaying = false;
    uint32_t lrnd = 0x1234567u;
    int engPos = 0;
    static constexpr int kChunk = 2048;              // render works in chunks this long at most
    // the control block: the envelopes advance once a block and the gains ramp across it, as the firmware's
    // (32 samples at 44.1 kHz, msfa's way: it also smooths each note's first instant); ~0.725 ms at any rate
    int ctl = 32;
    Patch patch;
    Derived d;
    Effects fx;
    // the operators' CUTOFF / FREQ slide to the patch's (two values of a preset change: no step; ~23 ms, as the
    // firmware's fofs); fofsOk false: the next render takes them as they are
    std::array<double, 6> fofs {};
    bool fofsOk = false;
    // the voices, on the heap (a voice is large with its MODAL banks: 16 of them inline made a processor ~0.4 MB, and
    // two on a test's stack overflowed it); sized here, once, never on the audio thread
    std::vector<Voice> voices = std::vector<Voice>(kMaxVoices);
    // the MACRO operators' models (FmMacro.h), one set for each voice's operator (voice x 6 + k), made in prepare()
    std::vector<std::unique_ptr<Macro, MacroDelete>> macros;
    SampleSlot sampleSlot;                       // SAMPLE: the recording handed over (FmSample.h)
    const SampleData* smp = nullptr;             // (the audio thread's: the one this block plays; nullptr none)
    uint64_t ageCounter = 0;
};

} // namespace usina::fm
