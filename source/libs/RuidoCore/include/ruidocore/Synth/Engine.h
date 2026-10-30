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

// Shared continuous-parameter interface. Engines interpret pitch, harmonics,
// timbre, morph and decay according to their own synthesis model.
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
    float accent = 1.0f;     // Output level is 0..1. Envelope depths are bipolar, -1..1, with zero disabled.
    // Pitch depth uses squared envelope amplitude scaled to 48 semitones; timbre
    // and morph use the envelope directly. Each engine must implement the applicable hooks.
    float pitchEnvAmount = 0.0f;
    float timbreEnvAmount = 0.0f;
    float morphEnvAmount = 0.0f;
    // The same internal envelope also controls harmonics and a final level trim.
    // Level modulation supplements the LPG amplitude envelope.
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
// Input may be nullptr for generators. Render writes the requested sample count;
// the caller owns buffers and manages voice mixing.
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

    // Read the internal decay envelope for host display or cross-engine modulation,
    // in the normalized range 0..1. Engines without that envelope may return zero.
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
// or a reference-style block-rate decay-time formula like
// `SemitonesToRatio(-96 * decay)` - see Dsp/DecayEnvelope.h's own users).
inline float semitonesToRatio(float semitones) {
    return std::exp2(semitones / 12.0f);
}

} //
