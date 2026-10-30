#include "ruidocore/Generator/Generator.h"
#include "ruidocore/Generator/TuringRegister.h"

#include <algorithm>
#include <array>
#include <random>

namespace ruidocore {

namespace {

using Grid16 = std::array<float, 16>;

// Canonical 16-step relative-weight profile per style: how likely each
// position is to be a hit in a typical example of that style, before
// density/regularity/syncopation reshape it. Where research described a
// RELATIONAL rule (e.g. "kick vs. bass interlock") rather than a literal
// step map, that's implemented as separate logic in generate()/
// decideActive() below, not squeezed into this single weight array.
Grid16 styleGrid(GeneratorStyle style) {
    switch (style) {
        case GeneratorStyle::Drum:
            // Deliberately ungrounded - a general-purpose baseline with no
            // named genre reference.
            return {1.0f, .05f, .1f, .05f, .3f, .05f, .1f, .05f,
                    .8f,  .05f, .1f, .05f, .3f, .05f, .2f, .05f};
        case GeneratorStyle::Industrial:
            // Rigid, machine-quantized 4/4 kick, snare-like hit on 2/4
            // (steps 4 and 12 of 16), no swing/humanization - the hat
            // layer's exact shape is a reasonable, unverified guess.
            return {1.0f, .7f, .7f, .7f, .9f, .7f, .7f, .7f,
                    1.0f, .7f, .7f, .7f, .9f, .7f, .7f, .7f};
        case GeneratorStyle::DrumAndNoise:
            return {1.0f, .3f, .3f, .3f, .5f, .3f, .3f, .3f,
                    .8f,  .3f, .3f, .3f, .5f, .3f, .4f, .3f};
        case GeneratorStyle::PowerNoise:
            // Strict 4/4 kick carrying tonal/melodic weight (not a
            // separate lead voice), density concentrated tightly on
            // downbeats with sparser, occasional off-grid fills.
            return {1.0f, .15f, .2f, .15f, .9f, .15f, .2f, .15f,
                    1.0f, .15f, .2f, .3f,  .9f, .3f,  .4f, .5f};
        case GeneratorStyle::Machine:
            // Steady, hypnotic, near-static 4/4 pattern ("motorik"). Not
            // literally zero variation - the flat grid pairs with a
            // near-zero-but-nonzero burst suppression in generate().
            return {.7f, .7f, .7f, .7f, .7f, .7f, .7f, .7f,
                    .7f, .7f, .7f, .7f, .7f, .7f, .7f, .7f};
        case GeneratorStyle::Glitch:
            return {.2f, .1f, .15f, .1f, .2f, .1f, .15f, .1f,
                    .2f, .1f, .15f, .1f, .2f, .1f, .15f, .1f};
        case GeneratorStyle::Ebm:
            // Rigid 4/4 kick - the interlocking bass line (see
            // secondaryGrid() below) is what carries the identity, not
            // the kick itself.
            return {1.0f, .05f, .05f, .05f, .05f, .05f, .05f, .05f,
                    1.0f, .05f, .05f, .05f, .05f, .05f, .05f, .05f};
        case GeneratorStyle::Gabber:
            // Kick on every downbeat with near-zero variation (regularity
            // is additionally floored to 1.0 for the kick part in
            // generate()); active, syncopated hihat/clap layer elsewhere.
            return {1.0f, .2f, .3f, .2f, 1.0f, .25f, .35f, .2f,
                    1.0f, .2f, .3f, .3f, 1.0f, .25f, .35f, .3f};
        case GeneratorStyle::DubTechno:
            // Understated 4/4 kick, sparse hats/clicks, lots of silence -
            // density is additionally clamped lower in generate().
            return {1.0f, .05f, .1f, .05f, .1f, .05f, .1f, .05f,
                    .9f,  .05f, .1f, .05f, .1f, .05f, .15f, .05f};
        case GeneratorStyle::Experimental:
            // Deliberately ungrounded, same reasoning as Drum above - the
            // one style with NO bespoke mechanism, a wide-open, fully
            // knob-driven baseline.
        default:
            return {.5f, .5f, .5f, .5f, .5f, .5f, .5f, .5f,
                    .5f, .5f, .5f, .5f, .5f, .5f, .5f, .5f};
    }
}

// EBM's defining rhythmic trait isn't the kick's shape - it's a busy,
// syncopated bass/sequencer line interlocking against a steady kick
// (classically a step-sequenced synth bass, not a drum voice). Applied to
// ONE specific part (see generate()) instead of rotated across all parts
// like the main styleGrid(), because it's describing a specific VOICE's
// role, not a general rhythmic density curve.
bool hasSecondaryVoice(GeneratorStyle style) { return style == GeneratorStyle::Ebm; }

Grid16 secondaryGrid(GeneratorStyle style) {
    switch (style) {
        case GeneratorStyle::Ebm:
            return {.9f, .3f, .6f, .3f, .2f, .7f, .3f, .6f,
                     .9f, .3f, .6f, .3f, .2f, .7f, .3f, .8f};
        default:
            return {.5f, .5f, .5f, .5f, .5f, .5f, .5f, .5f,
                    .5f, .5f, .5f, .5f, .5f, .5f, .5f, .5f};
    }
}

// Per-part "role" in the generated kit: a density multiplier (how busy
// this part tends to be relative to the global density knob) and a
// rotation applied to the style grid so parts don't all accent the same
// steps. Six roles - a conventional small drum kit shape, not an
// arbitrary part count.
using PartRole = GeneratorRole;

constexpr std::array<PartRole, 6> kPartRoles{{
    {0.65f, 0},  // Part 1: anchor (kick-like)
    {0.40f, 6},  // Part 2: backbeat (snare-like)
    {1.00f, 2},  // Part 3: dense layer (hat/noise texture)
    {0.50f, 9},  // Part 4: syncopated hit
    {0.30f, 4},  // Part 5: sparse accent/fx
    {0.35f, 11}, // Part 6: burst/glitch layer
}};

float gridWeightAt(const Grid16& grid, int rotation, int stepIndex, int length) {
    // Resample the canonical 16-step profile to the pattern's actual length.
    const int rotated = (stepIndex + rotation) % length;
    const int i16 = (rotated * 16) / std::max(1, length);
    return grid[static_cast<size_t>(i16 % 16)];
}

// One weighted, deterministic yes/no roll for whether a step is active.
bool decideActive(std::mt19937& rng, const GeneratorParams& params, const PartRole& role,
                   const Grid16& grid, int stepIndex) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    const float styleWeight = gridWeightAt(grid, role.rotation, stepIndex, params.length);
    const float randomWeight = unit(rng);
    // regularity=1 -> pure style grid; regularity=0 -> pure noise.
    float weight = styleWeight * params.regularity + randomWeight * (1.0f - params.regularity);

    // Syncopation shifts emphasis off the beat grid (assumes 1/16 steps,
    // 4 steps/beat - matches the default resolution).
    const bool onGrid = (stepIndex % 4) == 0;
    weight *= onGrid ? (1.0f - params.syncopation * 0.6f) : (1.0f + params.syncopation * 0.8f);

    const float probability =
        std::clamp(weight * role.densityMultiplier * (0.4f + params.density * 1.2f), 0.0f, 1.0f);
    return unit(rng) < probability;
}

// Motif/repeat logic itself lives in TuringRegister.h - this just supplies
// the per-step decision Generator's own style/weight logic makes.
std::vector<bool> generateActiveMask(std::mt19937& rng, const GeneratorParams& params, const PartRole& role,
                                      const Grid16& grid) {
    return turingRegisterMask(rng, params.length, params.repetition,
                               [&](int stepIndex) { return decideActive(rng, params, role, grid, stepIndex); });
}

// Injects short consecutive-hit clusters ("Burst"/"Stutter"), escalating
// into ratchets on dense/glitchy styles, plus an occasional "silence then
// explosion" macro-structure on the burst/glitch layer.
void applyBurst(std::mt19937& rng, const GeneratorParams& params, int partIndex, Part& part, bool writeVelocity) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    if (params.burst <= 0.0f)
        return;

    const int siteCount = std::max(1, static_cast<int>(params.burst * 3));
    std::uniform_int_distribution<int> posDist(0, std::max(0, part.length - 4));
    std::uniform_int_distribution<int> lenDist(2, 4);

    for (int s = 0; s < siteCount; ++s) {
        if (unit(rng) >= params.burst)
            continue;
        const int start = posDist(rng);
        const int burstLen = std::min(lenDist(rng), part.length - start);
        for (int i = 0; i < burstLen; ++i) {
            auto& ev = part.steps[static_cast<size_t>(start + i)];
            ev.enabled = true;
            if (writeVelocity)
                ev.velocity = std::max(ev.velocity, 90);

            // Accent ramps up across the cluster, peaking on its last hit.
            ev.accentAmount =
                std::max(ev.accentAmount, std::clamp(params.burst * (static_cast<float>(i + 1) / burstLen), 0.0f, 1.0f));
            if (i == burstLen - 1)
                ev.ratchet = 1 + static_cast<int>(std::round(params.burst * 3));
        }
    }

    // Silence-then-explosion macro - scoped to Drum'n'Noise and Power
    // Noise specifically, not every style. Only on the dedicated burst
    // layer (index 5), and only likely when both burst and density push
    // toward aggressive territory.
    const bool explosionEligible =
        params.style == GeneratorStyle::DrumAndNoise || params.style == GeneratorStyle::PowerNoise;
    if (explosionEligible && partIndex == 5 && unit(rng) < params.burst * params.density) {
        // Clamp burst length to the part length before computing the start index.
        const int explosionLen = std::min(part.length, std::max(2, part.length / 4));
        const int explosionStart = part.length - explosionLen;
        for (int i = 0; i < explosionStart; ++i)
            part.steps[static_cast<size_t>(i)].enabled = false;
        for (int i = 0; i < explosionLen; ++i) {
            auto& ev = part.steps[static_cast<size_t>(explosionStart + i)];
            ev.enabled = true;
            if (writeVelocity)
                ev.velocity = std::min(127, 100 + i * 6);
            ev.ratchet = 1 + (i % 3);
        }
    }
}

} // namespace

const char* toString(GeneratorStyle style) {
    switch (style) {
        case GeneratorStyle::Drum: return "Drum";
        case GeneratorStyle::Industrial: return "Industrial";
        case GeneratorStyle::DrumAndNoise: return "Drum'n'Noise";
        case GeneratorStyle::PowerNoise: return "Power Noise";
        case GeneratorStyle::Machine: return "Machine";
        case GeneratorStyle::Glitch: return "Glitch";
        case GeneratorStyle::Experimental: return "Experimental";
        case GeneratorStyle::Ebm: return "EBM";
        case GeneratorStyle::Gabber: return "Gabber";
        case GeneratorStyle::DubTechno: return "Dub Techno";
    }
    return "?";
}

const std::array<GeneratorPreset, kFactoryGeneratorPresetCount>& factoryGeneratorPresets() {
    static const std::array<GeneratorPreset, kFactoryGeneratorPresetCount> presets{{
        {"Tight Downbeat", {GeneratorStyle::Drum, 0.45f, 0.75f, 0.7f, 0.1f, 0.15f, 1.0f, 0.0f, 0.0f}},
        {"Dense Wall", {GeneratorStyle::Industrial, 0.8f, 0.55f, 0.5f, 0.3f, 0.2f, 0.6f, 0.0f, 0.0f}},
        {"Balanced Groove", {GeneratorStyle::DrumAndNoise, 0.6f, 0.6f, 0.55f, 0.3f, 0.3f, 0.8f, 0.1f, 0.0f}},
        {"Heavy Burst", {GeneratorStyle::PowerNoise, 0.7f, 0.5f, 0.4f, 0.7f, 0.35f, 1.0f, 0.0f, 0.0f}},
        {"Robotic Pulse", {GeneratorStyle::Machine, 0.65f, 0.9f, 0.85f, 0.05f, 0.05f, 0.3f, 0.0f, 0.0f}},
        {"Sparse Glitch", {GeneratorStyle::Glitch, 0.35f, 0.25f, 0.2f, 0.4f, 0.5f, 0.5f, 0.3f, 0.0f}},
        {"Wide Open", {GeneratorStyle::Experimental, 0.5f, 0.1f, 0.3f, 0.2f, 0.4f, 0.5f, 0.2f, 0.0f}},
        {"Interlocking Bassline", {GeneratorStyle::Ebm, 0.6f, 0.8f, 0.7f, 0.2f, 0.15f, 0.8f, 0.0f, 0.0f}},
        {"Four-on-the-Floor", {GeneratorStyle::Gabber, 0.75f, 0.85f, 0.75f, 0.25f, 0.1f, 1.0f, 0.0f, 0.0f}},
        {"Minimal & Sparse", {GeneratorStyle::DubTechno, 0.3f, 0.7f, 0.6f, 0.05f, 0.1f, 0.6f, 0.15f, 0.0f}},
    }};
    return presets;
}

Pattern Generator::generate(const GeneratorParams& params, const GeneratorProfile& profile) {
    Pattern pattern;
    pattern.bpm = params.bpm;
    pattern.swingPercent = std::clamp(params.swingPercent, 0.0f, 100.0f);
    const GeneratorRole* roles = profile.roles != nullptr ? profile.roles : kPartRoles.data();
    const int roleCount = profile.roles != nullptr ? profile.roleCount : static_cast<int>(kPartRoles.size());
    pattern.parts.resize(static_cast<size_t>(roleCount));

    const Grid16 grid = styleGrid(params.style);
    const bool glitchy = params.style == GeneratorStyle::Glitch || params.style == GeneratorStyle::Experimental ||
                          params.style == GeneratorStyle::PowerNoise;
    // Sub-step timing displacement + occasional stutter-repeats, distinct
    // from the generic "instability" above which only affects live re-roll
    // probability, not the STORED pattern's own timing/ratchet fields.
    // Glitch-only (not Experimental) so the two styles are mechanically
    // distinct, not just different weight grids.
    const bool idmStyle = params.style == GeneratorStyle::Glitch;
    // Industrial's trait is uniform, quantized, non-humanized hits ("wall
    // of sound") - the opposite of every other style's jitter.
    const bool suppressJitter = params.style == GeneratorStyle::Industrial;
    // Clamp density at the sparse end of the style range.
    const bool clampDensitySparse = params.style == GeneratorStyle::DubTechno;
    // Gabber's defining trait is a kick locked to every downbeat with
    // near-zero variation - overrides regularity for the kick part
    // specifically (part index 0, the "anchor" role).
    const bool lockKickRegularity = params.style == GeneratorStyle::Gabber;
    // Machine ("motorik"): minimal-fill, near-static, NOT literally
    // zero-variation - caps Burst very low rather than disabling it.
    const bool suppressBurstNearZero = params.style == GeneratorStyle::Machine;

    for (int partIndex = 0; partIndex < roleCount; ++partIndex) {
        const PartRole& role = roles[partIndex];

        // EBM's identity is a busy, syncopated bass/sequencer line
        // interlocking against the kick - carried by one dedicated part
        // (the dense-layer role) using its own grid, not the kick's.
        const bool usesSecondaryVoice = hasSecondaryVoice(params.style) && partIndex == 2;
        const Grid16& partGrid = usesSecondaryVoice ? secondaryGrid(params.style) : grid;

        GeneratorParams effParams = params;
        if (clampDensitySparse)
            effParams.density = std::min(effParams.density, 0.35f);
        if (lockKickRegularity && partIndex == 0)
            effParams.regularity = 1.0f;
        if (suppressBurstNearZero)
            effParams.burst = std::min(effParams.burst, 0.08f);

        // Deterministic but distinct stream per part, from the same seed.
        std::mt19937 rng(params.seed + static_cast<std::uint32_t>(partIndex) * 1000003u);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);

        Part part;
        part.outputId = partIndex + 1;
        part.length = params.length;
        part.steps.resize(static_cast<size_t>(profile.storageSteps > 0 ? profile.storageSteps : params.length));

        const std::vector<bool> activeMask = generateActiveMask(rng, effParams, role, partGrid);
        if (profile.pitchWalk != nullptr)
            profile.pitchWalk->beginPart(rng, params);
        for (int i = 0; i < params.length; ++i) {
            StepEvent& ev = part.steps[static_cast<size_t>(i)];
            ev.enabled = activeMask[static_cast<size_t>(i)];
            if (!ev.enabled)
                continue;

            if (profile.pitchWalk != nullptr && profile.walkAt == GeneratorProfile::WalkAt::BeforeDynamics)
                profile.pitchWalk->step(rng, effParams, ev);

            // Humanized velocity: more jitter as regularity drops.
            // Suppressed for Industrial (see suppressJitter above).
            if (profile.velocity) {
                const int baseVelocity = suppressJitter ? 105 : 85 + static_cast<int>(unit(rng) * 25.0f);
                const int jitter =
                    suppressJitter ? 0
                                    : static_cast<int>((unit(rng) - 0.5f) * 30.0f * (1.0f - effParams.regularity));
                ev.velocity = std::clamp(baseVelocity + jitter, 1, 127);
            }

            // Live-playback instability on glitch-leaning styles - never
            // mutates the stored pattern, only its chance to fire.
            ev.probability = glitchy ? std::clamp(1.0f - (1.0f - effParams.regularity) * 0.3f, 0.5f, 1.0f) : 1.0f;

            // Baseline accent from the style grid itself: on-grid/strong
            // positions read as "louder" even before any burst happens.
            // Dynamics pulls this toward a flat mid-tier as it drops:
            // dynamics=1 is full grid variation, dynamics=0 makes every
            // active step land at the same tier.
            const float gridWeight = gridWeightAt(partGrid, role.rotation, i, params.length);
            ev.accentAmount = std::clamp(0.5f + (gridWeight - 0.5f) * params.dynamics, 0.0f, 1.0f);

            // EBM bassline pitch: root-weighted, +-1 to +-2 semitones, one
            // +3 outlier - an informed-but-unverified design choice, not a
            // transcribed historical fact. The host application decides
            // how (or whether) to turn this into a real per-step pitch
            // lock/parameter - Generator itself has no synthesis-engine
            // access by design (pure function, no processor dependency).
            if (usesSecondaryVoice && profile.secondaryVoiceOffsets) {
                static constexpr std::array<int, 8> kEbmBassOffsets{0, 0, 0, -2, -1, 1, 2, 3};
                std::uniform_int_distribution<size_t> offsetPick(0, kEbmBassOffsets.size() - 1);
                ev.pitchSemitoneOffset = kEbmBassOffsets[offsetPick(rng)];
            } else if (profile.pitchWalk != nullptr && profile.walkAt == GeneratorProfile::WalkAt::AfterAccent) {
                profile.pitchWalk->step(rng, effParams, ev);
            }

            // IDM/Glitch: sub-step timing displacement + occasional
            // single-step stutter-repeats via ratchet. +/-20ms magnitude
            // is grounded in microtiming/asynchrony perception research
            // (the audible-but-musical range for percussive material sits
            // roughly 10-30ms); the 35%/12% occurrence rates are an
            // acknowledged, unvalidated aesthetic choice.
            if (idmStyle) {
                if (unit(rng) < 0.35f)
                    ev.microtimingMs = static_cast<int>((unit(rng) - 0.5f) * 40.0f); // +/-20ms nudge
                if (unit(rng) < 0.12f)
                    ev.ratchet = std::max(ev.ratchet, 2);
            }

            // Feel: general microtiming humanization, applies to every
            // style (stacks additively on top of Glitch's own harsher,
            // occasional displacement above). At feel=0 this is an exact
            // no-op.
            if (params.feel > 0.0f)
                ev.microtimingMs += static_cast<int>((unit(rng) - 0.5f) * 16.0f * params.feel); // up to +/-8ms
        }

        applyBurst(rng, effParams, partIndex, part, profile.velocity);
        if (profile.afterPart != nullptr)
            profile.afterPart(partIndex, part);

        // Store accent strength in the shared step; hosts map it to instrument-specific controls.

        pattern.parts[static_cast<size_t>(partIndex)] = std::move(part);
    }

    return pattern;
}

} //
