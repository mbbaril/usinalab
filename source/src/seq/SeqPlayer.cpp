// SPDX-License-Identifier: GPL-3.0-only
#include "SeqPlayer.h"
#include <ruidocore/Rhythm/TrigConditions.h>
#include <algorithm>
#include <cmath>

namespace usina::seq {

namespace {
constexpr int kMaxStepsPerBlock = 64;                    // (a host's stall never fires a burst: the family's cap)
constexpr double kResyncToleranceAt48k = 30.0;           // samples (the family's schedulers: a real host needs slack)
constexpr int64_t kSeekCycle = 16384;                    // steps replayed silently after a jump (see process)
} // namespace

ruidocore::Pattern defaultPattern()
{
    ruidocore::Pattern p;
    p.resolutionStepsPerBeat = 4;
    for (int lane = 0; lane < kLanes; ++lane) {
        ruidocore::Part part;
        part.outputId = 1;
        part.targetParamId = isNoteLane(lane) ? -1 : lane - 1;
        part.baseNote = lane >= kFirstDrumLane ? kDrumBaseNotes[lane - kFirstDrumLane] : 60;
        part.length = 16;
        ruidocore::ensureStepStorage(part);
        for (auto& s : part.steps) {
            s.enabled = false;
            s.velocity = isNoteLane(lane) ? 100 : 64;
            s.gateLength = 0.5f;
        }
        p.parts.push_back(std::move(part));
    }
    return p;
}

Player::Player()
{
    // Fixed storage is allocated once here, not on the audio thread or on the processor's host stack.
    // Keep fixed event storage off host/test stacks; it is allocated once, never in process().
    pending = std::make_unique<std::array<Pending, kMaxPending>>();
    chords = std::make_unique<std::array<ChordLane, kLanes>>();
    for (auto& s : slots) s = std::make_unique<Slot>();
    sequencer = std::make_unique<ruidocore::Sequencer>(defaultPattern());
    fired.resize(kLanes * 16);                           // (tick() fills it in place: grown here, never on the audio thread)
    for (auto& c : cursors) c.store(-1);
    for (auto& v : lanesOut) v.store(0.0f);
}

Player::~Player() = default;

void Player::prepare(double sampleRate)
{
    sr = sampleRate;
    wasPlaying = false;
    numPending = 0;
    expectedPpq = -1.0;
}

void Player::setPatternNow(const ruidocore::Pattern& p)
{
    ruidocore::Pattern q = p;
    while ((int)q.parts.size() < kLanes) q.parts.push_back(defaultPattern().parts[q.parts.size()]);
    q.parts.resize(kLanes);
    for (auto& part : q.parts) ruidocore::ensureStepStorage(part);
    sequencer->setPattern(std::move(q));
    for (auto& lane : *chords) for (auto& chord : lane) chord.count = 0;
    stepsPerBeat.store(std::clamp(p.resolutionStepsPerBeat, 1, 16));
    swing.store(std::clamp(p.swingPercent, 0.0f, 100.0f));
    wasPlaying = false;
    numPending = 0;
    expectedPpq = -1.0;
}

bool Player::submitLane(int lane, const ruidocore::Part& part, const ChordLane* noteSets)
{
    if (lane < 0 || lane >= kLanes) return false;
    collect();
    for (auto& s : slots) {
        if (s->state.load(std::memory_order_acquire) != 0) continue;
        s->lane = lane;
        s->part = part;                                  // (the copy: here, on the message thread)
        s->hasChords = noteSets != nullptr;
        if (noteSets) s->chords = *noteSets;
        ruidocore::ensureStepStorage(s->part);
        s->order = nextOrder++;
        s->state.store(1, std::memory_order_release);
        return true;
    }
    return false;
}

void Player::setGlobals(int spb, float sw)
{
    stepsPerBeat.store(std::clamp(spb, 1, 16));
    swing.store(std::clamp(sw, 0.0f, 100.0f));
}

void Player::collect()
{
    for (auto& s : slots)
        if (s->state.load(std::memory_order_acquire) == 2) {
            s->part = ruidocore::Part();                 // (the old lane freed here, never on the audio thread)
            s->state.store(0, std::memory_order_release);
        }
}

// the lanes that arrived, in the order they were sent
void Player::takeMail()
{
    for (;;) {
        Slot* first = nullptr;
        for (auto& s : slots)
            if (s->state.load(std::memory_order_acquire) == 1 && (first == nullptr || s->order < first->order))
                first = s.get();
        if (first == nullptr) return;
        sequencer->exchangePart(first->lane, first->part);
        if (first->hasChords) (*chords)[(size_t)first->lane] = first->chords;
        first->state.store(2, std::memory_order_release);
    }
}

// a note event at an absolute time (samples since the start). A note-on of a note still sounding takes that note's
// pending note-off to the same moment, so the two always come in pairs and in order
void Player::schedule(double at, bool on, int note, int velocity, bool musical, double musicalStart)
{
    if (on)
        for (int i = 0; i < numPending; ++i)
            if (!(*pending)[(size_t)i].on && (*pending)[(size_t)i].note == note && (*pending)[(size_t)i].at > at) (*pending)[(size_t)i].at = at;
    if (numPending >= kMaxPending) return;               // (fireStep makes room for both of a pair first)
    (*pending)[(size_t)numPending++] = { at, on, note, velocity, musical, musicalStart };
}

// stop or jump: every note sounding ends now; what was still to start does not
void Player::flushAll(Event* out, int maxOut, int& count)
{
    for (int i = 0; i < numPending; ++i)
        if (!(*pending)[(size_t)i].on && count < maxOut) out[count++] = { 0, false, (*pending)[(size_t)i].note, 0 };
    numPending = 0;
}

void Player::fireStep(const ruidocore::FiredStep& f, double stepStart, double stepSamples)
{
    const auto& pattern = sequencer->getPattern();
    const auto& part = pattern.parts[(size_t)f.partIndex];
    const auto& ev = f.event;
    if (isNoteLane(f.partIndex)) {
        if (mutes[(size_t)f.partIndex].load(std::memory_order_relaxed)) return;
        // played as the step comes: its probability, its condition (never written back into the pattern)
        if (ev.probability < 1.0f && std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) >= ev.probability) return;
        if (!ruidocore::evaluateTrigCondition(ev.condition, sequencer->loopCountForPart(f.partIndex), false)) return;
        const float sw = part.swingPercent != ruidocore::kSwingInherit ? part.swingPercent : pattern.swingPercent;
        const double swingOffset = (globalStep & 1) != 0 && sw > 0.0f ? (double)sw / 100.0 * 0.5 * stepSamples : 0.0;
        const double start = stepStart + swingOffset + std::max(0, ev.microtimingMs) * sr / 1000.0;
        const int ratchets = std::clamp(ev.ratchet, 1, 8);
        const double sub = stepSamples / ratchets;
        const int note = std::clamp(ruidocore::resolvedNote(ev, part) + ev.pitchSemitoneOffset, 0, 127);
        const int velocity = std::clamp((int)std::lround(ev.velocity + ev.accentAmount * (127.0f - (float)ev.velocity)), 1, 127);
        const double gate = std::max(1.0, (double)std::clamp(ev.gateLength, 0.01f, 1.0f) * sub);
        const auto& chord = (*chords)[(size_t)f.partIndex][(size_t)f.stepIndex];
        const int notes = std::clamp(chord.count, 1, kChordNotes);
        for (int r = 0; r < ratchets && numPending + 2 * notes <= kMaxPending; ++r) {
            // Reserve the whole chord's on/off pairs, so overload never leaves a partial chord or a lone on.
            for (int k = 0; k < notes; ++k) {
                const int pitch = chord.count > 0 ? std::clamp(chord.notes[(size_t)k] + ev.pitchSemitoneOffset, 0, 127) : note;
                const double on = start + r * sub;
                const float duration = chord.count > 0 ? chord.durations[(size_t)k] : 0.0f;
                const bool independent = std::isfinite(duration) && duration > 0.0f;
                const double noteGate = independent ? std::max(1.0, std::clamp((double)duration, .01, 64.0) * stepSamples *
                    std::clamp(part.clockDivider, 1, 16) / std::clamp(part.clockMultiplier, 1, 16)) : gate;
                schedule(on, true, pitch, velocity);
                schedule(on + noteGate, false, pitch, 0, independent, on);
            }
        }
    } else if (part.targetParamId >= 0 && part.targetParamId < kModLanes) {
        lanes[(size_t)part.targetParamId] = std::clamp((float)ev.velocity / 127.0f, 0.0f, 1.0f);
    }
}

int Player::process(int numSamples, const Transport& t, Event* out, int maxOut)
{
    int count = 0;
    takeMail();
    const int spb = stepsPerBeat.load();
    const float sw = swing.load();
    sequencer->setPatternGlobals(120.0, spb, sw);        // (the grid and the swing; the tempo is the host's)

    if (!t.playing) {
        if (wasPlaying) {                                // (stopped: every note ends now, the lanes start over next time)
            flushAll(out, maxOut, count);
            wasPlaying = false;
            for (auto& c : cursors) c.store(-1);
        }
        return count;
    }
    const bool bpmOk = std::isfinite(t.bpm) && t.bpm > 0.0;
    const double bpm = bpmOk ? std::clamp(t.bpm, 1.0, 999.0) : 120.0;
    // Keep explicit note lengths in musical time through tempo changes; legacy gates retain their exact behavior.
    if (wasPlaying && bpm != previousBpm)
        for (int i = 0; i < numPending; ++i)
            if ((*pending)[(size_t)i].musical && (*pending)[(size_t)i].at > blockStart) {
                auto& tail = (*pending)[(size_t)i];
                // A future microtimed/ratcheted onset stays put; only the note's musical duration scales.
                const double anchor = std::min(tail.at, std::max(blockStart, tail.musicalStart));
                tail.at = anchor + (tail.at - anchor) * previousBpm / bpm;
            }
    previousBpm = bpm;
    const double stepSamples = sr * 60.0 / bpm / spb;
    if (!wasPlaying) {                                   // (started: from the top, its grid from the host's position)
        wasPlaying = true;
        blockStart = nextBoundary = 0.0;
        globalStep = 0;
        expectedPpq = -1.0;
        numPending = 0;
        sequencer->resetCursors();
    }
    const double blockEnd = blockStart + numSamples;

    // the grid from the host's beat position: followed while it runs on, recomputed at any discontinuity
    const bool ppqOk = t.ppqValid && std::isfinite(t.ppq) && std::fabs(t.ppq) < 1.0e12 && bpmOk;
    if (ppqOk) {
        const double samplesPerBeat = sr * 60.0 / bpm;
        const double stepBeats = 1.0 / spb;
        const double tolerance = kResyncToleranceAt48k * (sr / 48000.0) / samplesPerBeat;
        if (expectedPpq < 0.0 || std::fabs(t.ppq - expectedPpq) > tolerance) {
            if (expectedPpq >= 0.0) flushAll(out, maxOut, count);   // (a jump, a loop: what sounds ends here)
            const int64_t next = (int64_t)std::ceil(t.ppq / stepBeats - 1.0e-9);
            globalStep = next;
            const double candidate = blockStart + ((double)next * stepBeats - t.ppq) * samplesPerBeat;
            nextBoundary = std::isfinite(candidate) && candidate > blockStart - sr * 5.0 ? candidate : blockEnd;
            // the lanes where the host's position puts them: from the top, then this many steps replayed silently
            // (the same place gives the same lanes - a loop in the host plays the same bars; past kSeekCycle steps,
            // 1024 bars of sixteenths, the position wraps)
            sequencer->resetCursors();
            for (int64_t i = 0, n = std::max<int64_t>(0, next) % kSeekCycle; i < n; ++i) sequencer->tick(fired);
        }
        expectedPpq = t.ppq + numSamples / sr * bpm / 60.0;
    } else {
        expectedPpq = -1.0;                              // (no position: it runs on its own, and resyncs once there is one)
    }

    for (int steps = 0; nextBoundary < blockEnd; ++steps) {
        if (steps >= kMaxStepsPerBlock) {
            nextBoundary = blockEnd;
            break;
        }
        const std::size_t n = sequencer->tick(fired);
        for (std::size_t i = 0; i < n; ++i) fireStep(fired[i], nextBoundary, stepSamples);
        ++globalStep;
        nextBoundary += stepSamples;
    }
    for (int lane = 0; lane < kLanes; ++lane) cursors[(size_t)lane].store(sequencer->cursorForPart(lane));

    // what falls in this block, in time order (a note-off before a note-on at the same sample)
    std::sort(pending->begin(), pending->begin() + numPending, [](const Pending& a, const Pending& b) {
        return a.at < b.at || (a.at == b.at && !a.on && b.on);
    });
    int kept = 0;
    for (int i = 0; i < numPending; ++i) {
        const auto& p = (*pending)[(size_t)i];
        if (p.at < blockEnd) {
            if (count < maxOut)
                out[count++] = { std::clamp((int)std::floor(p.at - blockStart), 0, numSamples - 1), p.on, p.note, p.velocity };
        } else {
            (*pending)[(size_t)kept++] = p;
        }
    }
    numPending = kept;
    blockStart = blockEnd;
    for (int m = 0; m < kModLanes; ++m) lanesOut[(size_t)m].store(lanes[(size_t)m]);
    return count;
}

float Player::laneValue(int modLane) const
{
    return modLane >= 0 && modLane < kModLanes ? lanesOut[(size_t)modLane].load() : 0.0f;
}

int Player::cursor(int lane) const { return lane >= 0 && lane < kLanes ? cursors[(size_t)lane].load() : -1; }

void Player::setMuted(int lane, bool m)
{
    if (lane >= 0 && lane < kLanes) mutes[(size_t)lane].store(m);
}

bool Player::muted(int lane) const { return lane >= 0 && lane < kLanes && mutes[(size_t)lane].load(); }

} // namespace usina::seq
