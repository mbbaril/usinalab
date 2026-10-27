#include "ruidocore/Sequencer/Sequencer.h"

#include <algorithm>

namespace ruidocore {

Sequencer::Sequencer(Pattern patternIn, PartStamp stampIn) : pattern(std::move(patternIn)), stamp(stampIn) {
    stampFrom(0);
    const size_t n = pattern.parts.size();
    cursor.assign(n, -1);
    pingPongDir.assign(n, 1);
    loopCount.assign(n, 0);
    pulseAccumulator.assign(n, 0);
    muted.assign(n, false);
    soloed.assign(n, false);
}

void Sequencer::stampFrom(size_t firstIndex) {
    if (stamp == nullptr)
        return;
    for (size_t i = firstIndex; i < pattern.parts.size(); ++i)
        stamp(static_cast<int>(i), pattern.parts[i]);
}

void Sequencer::setPatternGlobals(double bpm, int resolutionStepsPerBeat, float swingPercent) {
    pattern.bpm = bpm;
    pattern.resolutionStepsPerBeat = resolutionStepsPerBeat;
    pattern.swingPercent = swingPercent;
}

void Sequencer::setMute(int partIndex, bool isMuted) {
    if (partIndex >= 0 && partIndex < static_cast<int>(muted.size()))
        muted[static_cast<size_t>(partIndex)] = isMuted;
}

void Sequencer::setSolo(int partIndex, bool isSoloed) {
    if (partIndex >= 0 && partIndex < static_cast<int>(soloed.size()))
        soloed[static_cast<size_t>(partIndex)] = isSoloed;
}

void Sequencer::resetCursors() {
    std::fill(cursor.begin(), cursor.end(), -1);
    std::fill(pingPongDir.begin(), pingPongDir.end(), 1);
    std::fill(loopCount.begin(), loopCount.end(), 0);
    std::fill(pulseAccumulator.begin(), pulseAccumulator.end(), 0);
}

void Sequencer::setPattern(Pattern newPattern) {
    pattern = std::move(newPattern);
    stampFrom(0);
    const size_t n = pattern.parts.size();
    cursor.assign(n, -1);
    pingPongDir.assign(n, 1);
    loopCount.assign(n, 0);
    pulseAccumulator.assign(n, 0);
    muted.assign(n, false);
    soloed.assign(n, false);
}

void Sequencer::addPart(Part part) {
    if (stamp != nullptr)
        stamp(static_cast<int>(pattern.parts.size()), part);
    pattern.parts.push_back(std::move(part));
    cursor.push_back(-1);
    pingPongDir.push_back(1);
    loopCount.push_back(0);
    pulseAccumulator.push_back(0);
    muted.push_back(false);
    soloed.push_back(false);
}

void Sequencer::removePart(int partIndex) {
    if (partIndex < 0 || partIndex >= static_cast<int>(pattern.parts.size()))
        return;
    const auto i = static_cast<size_t>(partIndex);
    pattern.parts.erase(pattern.parts.begin() + static_cast<long>(i));
    cursor.erase(cursor.begin() + static_cast<long>(i));
    pingPongDir.erase(pingPongDir.begin() + static_cast<long>(i));
    loopCount.erase(loopCount.begin() + static_cast<long>(i));
    pulseAccumulator.erase(pulseAccumulator.begin() + static_cast<long>(i));
    muted.erase(muted.begin() + static_cast<long>(i));
    soloed.erase(soloed.begin() + static_cast<long>(i));
    stampFrom(i); // every Part above moved down one index - re-derive what the index decides
}

void Sequencer::setPart(int partIndex, Part newPart) {
    if (partIndex < 0 || partIndex >= static_cast<int>(pattern.parts.size()))
        return;
    const size_t i = static_cast<size_t>(partIndex);
    if (newPart.length > 0 && cursor[i] >= newPart.length)
        cursor[i] = cursor[i] % newPart.length; // clamp, don't reset - keeps this part in phase with the others
    if (stamp != nullptr)
        stamp(partIndex, newPart);
    pattern.parts[i] = std::move(newPart);
}

void Sequencer::exchangePart(int partIndex, Part& part) {
    if (partIndex < 0 || partIndex >= static_cast<int>(pattern.parts.size()))
        return;
    const size_t i = static_cast<size_t>(partIndex);
    if (part.length > 0 && cursor[i] >= part.length)
        cursor[i] = cursor[i] % part.length; // (as setPart)
    if (stamp != nullptr)
        stamp(partIndex, part);
    std::swap(pattern.parts[i], part); // (member-wise swaps: no allocation, no free)
}

int Sequencer::advanceCursor(int partIndex) {
    const size_t i = static_cast<size_t>(partIndex);
    const Part& part = pattern.parts[i];
    const int length = part.length;
    if (length <= 0)
        return 0;
    if (length == 1) {
        cursor[i] = 0;
        return 0;
    }

    switch (part.direction) {
        case Direction::Forward:
            cursor[i] = (cursor[i] + 1) % length;
            break;

        case Direction::Reverse:
            cursor[i] = ((cursor[i] - 1) % length + length) % length;
            break;

        case Direction::PingPong: {
            if (cursor[i] < 0) {
                cursor[i] = 0;
                pingPongDir[i] = 1;
                break;
            }
            int next = cursor[i] + pingPongDir[i];
            if (next >= length) {
                pingPongDir[i] = -1;
                next = length - 2;
            } else if (next < 0) {
                pingPongDir[i] = 1;
                next = 1;
            }
            cursor[i] = std::clamp(next, 0, length - 1);
            break;
        }

        case Direction::Random: {
            std::uniform_int_distribution<int> dist(0, length - 1);
            cursor[i] = dist(rng);
            break;
        }
    }
    return cursor[i];
}

std::vector<FiredStep> Sequencer::tick() {
    // The allocating form, kept for the console prototypes and the tests,
    // where one allocation per call costs nothing. Every real-time caller
    // should use the overload below.
    std::vector<FiredStep> result;
    const std::size_t count = tick(result);
    result.resize(count);
    return result;
}

std::size_t Sequencer::tick(std::vector<FiredStep>& out) {
    std::size_t fired = 0;
    const bool anySolo = std::any_of(soloed.begin(), soloed.end(), [](bool s) { return s; });

    for (int i = 0; i < static_cast<int>(pattern.parts.size()); ++i) {
        const Part& part = pattern.parts[static_cast<size_t>(i)];
        if (part.length <= 0 || part.steps.empty())
            continue;

        const size_t si = static_cast<size_t>(i);
        // The Part's own clock, unless something is bending it right now -
        // see setClockWarp. Read fresh every pulse rather than cached: a
        // warp that lasts three steps has to end on its own.
        int multiplier = std::max(1, part.clockMultiplier);
        int divider = std::max(1, part.clockDivider);
        if (clockWarp != nullptr) {
            clockWarp(clockWarpContext, i, multiplier, divider);
            multiplier = std::max(1, multiplier);
            divider = std::max(1, divider);
        }
        pulseAccumulator[si] += multiplier;
        while (pulseAccumulator[si] >= divider) {
            pulseAccumulator[si] -= divider;

            const int prevCursor = cursor[si];
            const int idx = advanceCursor(i);
            if (idx < 0 || idx >= static_cast<int>(part.steps.size()))
                continue;
            if (idx == 0 && prevCursor != -1)
                ++loopCount[si];

            const bool audible = !muted[si] && (!anySolo || soloed[si]);
            if (!audible)
                continue;

            const StepEvent& ev = part.steps[static_cast<size_t>(idx)];
            if (!ev.enabled)
                continue;

            // Resolve the note HERE, once, for everything downstream
            // (2026-09-16). A fired step always leaves the sequencer with
            // an explicit note - the step's own, or its Part's baseNote -
            // so no scheduler, MIDI writer or synthesis engine has to
            // know that "unset" exists. This is the one playback choke
            // point; resolving anywhere later would be a place to forget.
            // Written INTO the caller's buffer rather than pushed as a new
            // object: assigning over a slot that already exists reuses its
            // StepEvent's `locks` storage instead of allocating a fresh
            // vector for every hit, which is where most of the audio
            // thread's allocations were coming from.
            if (fired == out.size())
                out.emplace_back();
            FiredStep& slot = out[fired++];
            slot.partIndex = i;
            slot.outputId = part.outputId;
            slot.stepIndex = idx;
            slot.event = ev;
            slot.event.explicitNote = resolvedNote(ev, part);
        }
    }
    return fired;
}

} // namespace ruidocore
