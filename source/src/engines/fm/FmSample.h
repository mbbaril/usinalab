// SPDX-License-Identifier: GPL-3.0-only
// Usina SAMPLE operator: pitched playback of a file or captured input.
// The algorithm routes its output as audio, excitation or phase modulation.
// Incoming modulation offsets the sample read phase.
//   A READ (a list): ONCE, LOOP, PING-PONG, REVERSE (once, from the end)
//   B START 0..100 (% of the recording)  C LENGTH 0..200 (0..100 % of what is left; 1 ms at least)
//   D LOOP: XFADE 0..100 - the seam crossfaded over up to half the loop (no click where it joins); ONCE, REVERSE,
//     PING-PONG: FADE 0..100 - the reading's own fades, raised cosine, 1 ms (0) .. 200 ms (100), in at its start and
//     (ONCE, REVERSE) out before the region's end
// Raised-cosine fades prevent discontinuities at region boundaries and voice
// replacement: at least 1 ms for audible operators and 5 ms for modulators.
// A replaced voice crossfades its previous readout into the new one.
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

// the reading's fade, ms: LOOP keeps D for its seam (its start: the de-click); the others D's FADE, 1 .. 200 ms -
// never under the de-click (1 ms heard, 5 ms modulating)
constexpr double kSampleDeclickMs = 1.0, kSampleModDeclickMs = 5.0, kSampleTailMs = 3.0;
inline double sampleFadeMs(int read, double d, double declickMs = kSampleDeclickMs)
{
    return read == SR_LOOP ? declickMs : std::max(declickMs, std::pow(200.0, std::clamp(d, 0.0, 100.0) / 100.0));
}
inline float raisedCosine(double x)                      // (0 at 0, 1 at 1; smooth at both ends)
{
    return (float)(0.5 - 0.5 * std::cos(3.14159265358979323846 * std::clamp(x, 0.0, 1.0)));
}

// one voice's reading of it
struct SamplePlay {
    double pos = 0.0;
    int dir = 1;
    bool done = true;
    double age = 0.0;                            // output samples since the start (the fade in)
    float gain = 0.0f;                           // the fades' gain on the last sample
    // the reading a new start took over while it still sounded: faded out over kSampleTailMs from its place
    double tpos = 0.0, tleft = 0.0, tlen = 0.0;
    int tdir = 1;
    float tgain = 0.0f;
    bool tailPending = false;

    void start(int read, double a, double b)
    {
        if (!done && gain > 0.0f) {                       // (still sounding: its reading goes out, not cut)
            tpos = pos;
            tdir = dir;
            tgain = gain;
            tailPending = true;
        }
        dir = read == SR_REVERSE ? -1 : 1;
        pos = read == SR_REVERSE ? b : a;
        done = false;
        age = 0.0;
        gain = 0.0f;
    }
    // one sample: step = source samples a sample (the pitch), pm = the input's offset (source samples), d = D,
    // perMs = output samples a millisecond (the fades are in the output's time: the same at any pitch)
    float process(const SampleData& s, int read, double a, double b, double d, double step, double pm, double perMs,
                  double declickMs = kSampleDeclickMs)
    {
        float tail = 0.0f;
        if (tailPending) {
            tailPending = false;
            tlen = tleft = std::max(1.0, std::max(kSampleTailMs, declickMs) * perMs);
        }
        if (tleft > 0.0) {                                // (the old reading, its gain going to 0)
            tail = sampleAt(s, tpos + pm) * tgain * raisedCosine(tleft / tlen);
            tpos += tdir * step;
            tleft = tpos < 0.0 || tpos > (double)s.data.size() - 1.0 ? 0.0 : tleft - 1.0;
        }
        if (done) return tail;
        float y = sampleAt(s, pos + pm);
        if (read == SR_LOOP) {                                // the seam: the loop's end crossfaded into its start
            const double len = b - a;
            const double xf = std::min({ len * 0.5 * std::clamp(d / 100.0, 0.0, 1.0), a, len });
            if (xf > 1.0 && pos > b - xf) {
                const float w = (float)((pos - (b - xf)) / xf);
                y = (1.0f - w) * y + w * sampleAt(s, pos - len + pm);
            }
        }
        // the fades: in from the start; out before the end (ONCE, REVERSE); both shortened together when the region
        // is shorter than the two (the region's length in output samples, at this pitch)
        double fin = sampleFadeMs(read, d, declickMs) * perMs, fout = read == SR_ONCE || read == SR_REVERSE ? fin : 0.0;
        if (read == SR_LOOP) fout = 0.0;
        const double regionOut = (b - a) / std::max(step, 1.0e-9);
        if (fin + fout > regionOut && regionOut > 0.0) {
            const double k = regionOut / (fin + fout);
            fin *= k;
            fout *= k;
        }
        float g = fin > 0.0 ? raisedCosine(age / fin) : 1.0f;
        if (fout > 0.0) {
            const double left = (dir > 0 ? b - pos : pos - a) / std::max(step, 1.0e-9);   // (output samples to the end)
            g *= raisedCosine(left / fout);
        }
        gain = g;
        age += 1.0;
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
        if (done) gain = 0.0f;
        return y * g + tail;
    }
};

} // namespace usina::fm
