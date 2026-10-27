#pragma once

#include <cmath>
#include <cstddef>

namespace ruidocore {

// Shared trigger semantics across every synthesis engine - lets an engine
// distinguish "no trigger source connected at all" from "connected but
// currently low," which matters for engines that behave differently in
// free-running vs. triggered mode. Same shape proven across real
// multi-algorithm engines researched for this library (see
// docs/SYNTH_ENGINE_RESEARCH.md).
enum class TriggerState { Low, RisingEdge, Unpatched, High };

// One small, shared continuous-parameter surface, reinterpreted per
// engine. This is the single most load-bearing design decision in this
// module: every synthesis algorithm reads from the SAME few knobs
// (harmonics/timbre/morph + note/trigger/decay/accent) and decides what
// they mean for itself (e.g. for FM: harmonics = operator ratio, timbre =
// modulation index, morph = feedback). This is what makes a generic
// modulation matrix (RuidoCore's own Mod engine, already built) usable
// across different synthesis algorithms without per-algorithm special
// casing - a MOD generator just targets "harmonics" or "timbre" as a
// paramId, same as it would target any other parameter.
//
// `decay` is a deliberate addition beyond the pure 3-knob
// (harmonics/timbre/morph) shape documented in the research notes - this
// library's first real host is a drum machine, where per-hit decay time
// is a first-class control, not something exclusively handled by an
// external envelope stage. Engines that don't need it are free to ignore
// it.
struct EngineParameters {
    TriggerState trigger = TriggerState::Unpatched;
    float note = 60.0f;      // MIDI-note-numbered pitch, fractional (for pitch bend/drift)
    float harmonics = 0.5f;  // 0..1
    float timbre = 0.5f;     // 0..1
    float morph = 0.5f;      // 0..1
    float decay = 0.5f;      // 0..1, engine-defined mapping to an actual time
    float accent = 1.0f;     // 0..1, scales output level - kept separate from `timbre`

    // Bipolar depth (-1..1, 0 = off) for an engine's own built-in
    // Dsp/DecayEnvelope.h-driven "punch" on trigger - the same internal-
    // envelope-normalled-to-CV mechanism real Plaits voices have
    // (2026-08-27, "o plaits possui um envelope interno que pode ser
    // usado pra criar um pitch envelope também" -> "seria legal poder
    // atribuir ele também aos outros knobs" - real Plaits normals this
    // ONE shared envelope to all three of its FM/TIMBRE/MORPH CV inputs
    // simultaneously, not just pitch, so all three destinations are
    // exposed here the same way). `pitchEnvAmount` squares the envelope
    // and scales by 48 semitones (a sharp transient spike, matching
    // Plaits' own formula); `timbreEnvAmount`/`morphEnvAmount` add the
    // envelope's value directly (unsquared), matching Plaits' own
    // simpler direct-modulation formula for those two. An engine that
    // doesn't implement any of these is free to ignore them, same as
    // `decay` above - BUT "free to ignore" is a silent, easy-to-forget
    // trap for whoever writes the NEXT engine: as of 2026-08-28 only
    // FmEngine/WavetableEngine/FilteredNoiseEngine/ParticleNoiseEngine/
    // SwarmEngine actually read these 5 fields (grep for "EnvAmount" in
    // this directory to check current coverage before trusting this
    // list) - BassDrumEngine/SnareDrumEngine/HiHatEngine/ModalEngine/
    // StringEngine do NOT, so their own internal-envelope knobs (exposed
    // in every host UI regardless of engine, since this struct doesn't
    // vary per engine) turn/read but produce ZERO audible change - a
    // real user-facing bug ("os envelopes nao parecem ter efeito",
    // caught 2026-08-28 with a KICK part set to String). EVERY new
    // engine MUST either wire this same DecayEnvelope-driven hook (see
    // FmEngine.h's own `render()` for the reference implementation) or
    // get a deliberate, written-down reason not to - never leave it
    // silently unimplemented the way the 5 above currently are.
    float pitchEnvAmount = 0.0f;
    float timbreEnvAmount = 0.0f;
    float morphEnvAmount = 0.0f;
    // Real Plaits only normals its internal envelope to FM/TIMBRE/MORPH
    // (a hardware limit - those are the only 3 CV inputs it has).
    // 2026-08-27, user: "não temos essa limitação aqui. pode adicionar
    // aos outros" - this is software, so the same envelope is exposed to
    // Harmonics and Level (a plugin-level gain trim on top of whatever
    // amplitude shaping the engine's own LPG already does) too, with no
    // hardware reason to hold back.
    float harmonicsEnvAmount = 0.0f;
    float levelEnvAmount = 0.0f;
};

// Lets a voice/mixer stage know whether this engine already rendered its
// own amplitude envelope (a percussive/self-enveloping algorithm, e.g. a
// modal drum body) or expects an external envelope/gate stage applied on
// top (a sustained tonal algorithm). Building this as an explicit flag
// keeps every engine going through the same voice-management path
// without the host needing to special-case "is this a drum or a synth."
struct PostProcessingSettings {
    float outGain = 1.0f;
    float auxGain = 1.0f;
    bool alreadyEnveloped = false;
};

// Base class for every synthesis algorithm. `init`/`reset` are separate
// (matching the "clear + re-run Init" pattern used for cheap engine
// switching in the engines this was researched from) - `reset()` clears
// only runtime/playback state (phase, envelope position), `init()` also
// (re)establishes anything sample-rate-dependent.
//
// `render` fills BOTH `out` (the main signal) and `aux` (a second,
// engine-defined signal - a sub-oscillator, the raw modulator, an impact
// layer, whatever this specific algorithm can usefully expose) - a host
// is free to ignore `aux` or route it through its own effect chain.
//
// `in` is an OPTIONAL input buffer (nullptr for a pure generator that has
// nothing to read). This one signature deliberately covers both roles a
// synthesis algorithm can play (2026-08-27, "o rings e clouds funcionam
// como processadores de audio também" / "trate eles de forma modular"):
// a pure generator (this library's own `FmEngine`) simply ignores `in`;
// a resonator/granular-style algorithm that EXCITES itself from an
// external audio signal (a modal resonator bank driven by a real
// impulse/audio input, a granular processor reading a live input instead
// of only a stored buffer) reads from it. Engines that never use `in`
// don't need a separate base class or interface - the same `Engine`
// covers both, so a host's voice-management code never has to special-
// case "is this one a generator or a processor."
class Engine {
public:
    virtual ~Engine() = default;

    virtual void init(double sampleRate) = 0;
    virtual void reset() = 0;
    virtual void render(const EngineParameters& parameters, const float* in, float* out, float* aux,
                         size_t numSamples) = 0;

    // Lets a host's voice manager know whether this instance still has
    // anything to render (a self-enveloping percussive voice like
    // `FmEngine` goes inactive once its own envelope decays to silence) -
    // used to decide which voice a new note-on can safely reuse without
    // an audible cutoff. Default true (the safe assumption for an
    // always-on processor/sustained engine that has no natural "done"
    // state of its own); `FmEngine` overrides this with its real gate
    // state.
    virtual bool isActive() const { return true; }

    // Live readback of this instance's own internal punch envelope
    // (Dsp/DecayEnvelope.h), 0..1, for a host UI to visualize the SAME
    // curve pitchEnvAmount/timbreEnvAmount/etc above actually modulate
    // with in real time (2026-08-28, "e ainda assim nao ha o movimento
    // dos knobs usando o envelope hardwired" - a fixed on-trigger flash
    // pulse isn't the same thing as a live trace of the real decay
    // curve, which is what this exists for). Default 0 (matches
    // `isActive`'s own "safe assumption for an engine with no such
    // state" pattern) - only the 5 engines that actually implement the
    // pitch/timbre/morph/harmonics/level EnvAmount hook above (see that
    // struct's own comment for which ones, and re-grep before trusting
    // it) override this to `return decayEnvelope.value();`. A NEW
    // engine that wires up the EnvAmount hook MUST also override this
    // at the same time - it is the other half of the same feature, not
    // an optional extra.
    virtual float getEnvelopeValue01() const { return 0.0f; }

    PostProcessingSettings postProcessing;
};

// Converts a MIDI-style note number (60 = middle C / C4) to frequency in
// Hz. Kept here (not per-engine) so every engine agrees on the same
// tuning reference.
inline float noteToFrequencyHz(float noteNumber) {
    return 440.0f * std::exp2((noteNumber - 69.0f) / 12.0f);
}

// Shared by any engine that needs to convert a semitone interval into a
// linear frequency ratio (e.g. FmEngine's ratio-quantizer LUT lookup,
// or a Plaits-style block-rate decay-time formula like
// `SemitonesToRatio(-96 * decay)` - see Dsp/DecayEnvelope.h's own users).
inline float semitonesToRatio(float semitones) {
    return std::exp2(semitones / 12.0f);
}

} // namespace ruidocore
