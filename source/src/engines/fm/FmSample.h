// SPDX-License-Identifier: GPL-3.0-only
// Usina FM engine - SAMPLE (27/10/2026; the user, approving its four points: "mande ver e vamos ser felizes com algo
// que não há"): an operator that plays a recording - loaded from a file or captured from the input - at its pitch.
// Through the algorithm it goes where any operator goes: into a MODAL operator it is the excitation (a can struck,
// heard as a bell), through the TEXTURE it is grains, and as a modulator it moves the other operators' phase: a sound
// of the world as FM. Its input (FM) moves the reading place: phase modulation of the recording itself.
//   A READ (a list): ONCE, LOOP, PING-PONG, REVERSE (once, from the end)
//   B START 0..100 (% of the recording)  C LENGTH 0..200 (0..100 % of what is left; 1 ms at least)
//   D XFADE 0..100: LOOP's seam crossfaded over up to half the loop (no click where it joins)
// Pitch: the note (with COARSE, FINE, FIXED... as any operator) against the root C3 (MIDI 60): played at C3 the
// recording is heard as it is. Read with 4-point Hermite interpolation at the engine's (oversampled) rate.
// Nothing here allocates or frees on the audio thread: SampleSlot hands a recording over and takes the old one back.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace usina::fm {

struct SampleData {
    std::vector<float> data;                     // mono, -1..1; never changed once handed over
    double rate = 48000.0;                       // its own sample rate
};

constexpr double kSampleRootHz = 261.6255653005986;   // C3 (MIDI 60): the recording as it is
enum SampleRead { SR_ONCE, SR_LOOP, SR_PINGPONG, SR_REVERSE, kSampleReads };
inline const char* sampleReadName(int r)
{
    static const char* const N[kSampleReads] = { "ONCE", "LOOP", "PING-PONG", "REVERSE" };
    return N[std::clamp(r, 0, kSampleReads - 1)];
}

// the handover between the message thread (give, collect) and the audio thread (take, at a block's start): a new
// recording waits in `incoming`; the audio thread takes it only when the one it drops can go to `outgoing` (empty),
// where the message thread frees it. One writer each side; nothing freed where audio runs
class SampleSlot {
public:
    SampleSlot() = default;
    SampleSlot(const SampleSlot&) = delete;
    SampleSlot& operator=(const SampleSlot&) = delete;
    ~SampleSlot()
    {
        delete incoming.exchange(nullptr);
        delete outgoing.exchange(nullptr);
        delete current;
    }
    // (message thread) the next recording; nullptr or an empty one: none. One not taken yet is replaced (freed here)
    void give(std::unique_ptr<SampleData> s)
    {
        collect();
        delete incoming.exchange(s ? s.release() : new SampleData(), std::memory_order_acq_rel);
    }
    void collect() { delete outgoing.exchange(nullptr, std::memory_order_acq_rel); }   // (message thread)
    // (audio thread) the recording to play from now; a new one only when the old one has somewhere to go
    const SampleData* take()
    {
        if (outgoing.load(std::memory_order_acquire) == nullptr)
            if (SampleData* p = incoming.exchange(nullptr, std::memory_order_acq_rel)) {
                if (current != nullptr) outgoing.store(current, std::memory_order_release);
                current = p;
            }
        return current != nullptr && !current->data.empty() ? current : nullptr;
    }

private:
    std::atomic<SampleData*> incoming { nullptr }, outgoing { nullptr };
    SampleData* current = nullptr;               // (the audio thread's)
};

// where the region starts and ends, in the recording's samples (START, LENGTH)
inline void sampleRegion(const SampleData& s, double start, double length, double& a, double& b)
{
    const double last = (double)s.data.size() - 1.0;
    a = std::clamp(start / 100.0, 0.0, 1.0) * last;
    const double rest = last - a;
    b = a + std::min(rest, std::max(s.rate * 0.001, rest * std::clamp(length / 200.0, 0.0, 1.0)));
}

// 4-point, 3rd-order Hermite at pos (the ends held)
inline float sampleAt(const SampleData& s, double pos)
{
    const auto n = (long)s.data.size();
    const double fl = std::floor(pos);
    const long i = (long)fl;
    const float f = (float)(pos - fl);
    const auto at = [&](long j) { return s.data[(size_t)std::clamp(j, 0L, n - 1)]; };
    const float xm1 = at(i - 1), x0 = at(i), x1 = at(i + 1), x2 = at(i + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

// one voice's reading of it
struct SamplePlay {
    double pos = 0.0;
    int dir = 1;
    bool done = true;

    void start(int read, double a, double b)
    {
        dir = read == SR_REVERSE ? -1 : 1;
        pos = read == SR_REVERSE ? b : a;
        done = false;
    }
    // one sample: step = source samples a sample (the pitch), pm = the input's offset, in source samples
    float process(const SampleData& s, int read, double a, double b, double xfade, double step, double pm)
    {
        if (done) return 0.0f;
        float y = sampleAt(s, pos + pm);
        if (read == SR_LOOP) {                                // the seam: the loop's end crossfaded into its start
            const double len = b - a;
            const double xf = std::min({ len * 0.5 * std::clamp(xfade / 100.0, 0.0, 1.0), a, len });
            if (xf > 1.0 && pos > b - xf) {
                const float w = (float)((pos - (b - xf)) / xf);
                y = (1.0f - w) * y + w * sampleAt(s, pos - len + pm);
            }
        }
        pos += dir * step;
        switch (read) {
        case SR_LOOP:
            if (b - a < 1.0) done = true;
            else while (pos >= b) pos -= b - a;
            break;
        case SR_PINGPONG:
            if (b - a < 1.0) { done = true; break; }
            for (int guard = 0; guard < 4 && (pos > b || pos < a); ++guard) {
                if (pos > b) { pos = b - (pos - b); dir = -1; }
                if (pos < a) { pos = a + (a - pos); dir = 1; }
            }
            break;
        case SR_REVERSE: if (pos < a) done = true; break;
        default: if (pos > b) done = true; break;         // ONCE
        }
        return y;
    }
};

} // namespace usina::fm
