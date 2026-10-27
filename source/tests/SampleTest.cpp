// SPDX-License-Identifier: GPL-3.0-only
// The SAMPLE operator, measured (rule 25; engines/fm/FmSample.h): a recording played, its sound analysed.
//   PITCH     at C3 the recording as it is (a 441 Hz sine: 441 Hz), at C4 an octave up
//   ONCE      it ends where the region ends (silence after, the envelope still open)
//   REVERSE   a rising ramp heard falling
//   XFADE     a loop whose seam jumps: crossfaded, the largest step between two samples much smaller
//   MODAL     into a MODAL operator (STRIKE 0: only what comes in sounds it), the object rings; without it, silence
//   HANDOVER  recordings given while it renders: none allocated or freed on the render's side
#include "engines/fm/FmEngine.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

// every allocation and free in this program, counted (the render must cause none)
static std::atomic<long> allocations { 0 }, frees { 0 };
void* operator new(std::size_t n)
{
    ++allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept
{
    if (p) ++frees;
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    if (p) ++frees;
    std::free(p);
}

namespace {
using namespace usina::fm;
constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;

int failures = 0;
void check(bool ok, const char* what, double got, const char* want)
{
    std::printf("%-52s %s  %10.3f  (want %s)\n", what, ok ? "ok  " : "FAIL", got, want);
    failures += ok ? 0 : 1;
}

std::unique_ptr<SampleData> recording(int n, double rate, double (*f)(int, int))
{
    auto s = std::make_unique<SampleData>();
    s->rate = rate;
    s->data.resize((size_t)n);
    for (int i = 0; i < n; ++i) s->data[(size_t)i] = (float)f(i, n);
    return s;
}

// OP1 SAMPLE alone (ALGO 1: OP2 -> OP1, OP2 silent), the envelope open
std::map<std::string, int> sampler(int read, int start, int length, int xfade)
{
    return { { "ALGO", 0 }, { "OP1.MODE", SAMPLE }, { "OP1.A", read }, { "OP1.B", start }, { "OP1.C", length },
             { "OP1.D", xfade }, { "OP1.LEVEL", 100 }, { "OP1.SUS", 100 }, { "OP1.REL", 20 } };
}

std::vector<float> render(Engine& e, const std::map<std::string, int>& vals, int note, double secs)
{
    e.setPatch(Patch::fromValues(vals));
    e.noteOn(note, 127);
    const int n = (int)(secs * kFs), blk = 256;
    std::vector<float> L((size_t)n, 0.0f), R((size_t)n, 0.0f);
    for (int i = 0; i < n; i += blk) e.render(L.data() + i, R.data() + i, std::min(blk, n - i));
    e.noteOff(note);
    return L;
}

// the frequency by the rising zero crossings over x[a, b)
double frequency(const std::vector<float>& x, size_t a, size_t b)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (size_t i = a + 1; i < b; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double t = (double)(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0.0) first = t;
            last = t;
            ++count;
        }
    return count > 1 ? (count - 1) * kFs / (last - first) : 0.0;
}

double rms(const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0.0;
    for (size_t i = a; i < b && i < x.size(); ++i) s += (double)x[i] * x[i];
    return std::sqrt(s / (double)std::max<size_t>(1, b - a));
}
} // namespace

int main()
{
    // PITCH: a 441 Hz sine recorded at 44.1 kHz, played at 48 kHz: C3 hears 441 Hz, C4 882 Hz
    {
        auto e = std::make_unique<Engine>();
        e->prepare(kFs, false, kFs);
        e->setSample(recording(44100, 44100.0, [](int i, int) { return 0.8 * std::sin(2.0 * kPi * 441.0 * i / 44100.0); }));
        const auto c3 = render(*e, sampler(SR_ONCE, 0, 200, 0), 60, 0.5);
        const double f3 = frequency(c3, 2400, 21600);
        check(std::fabs(f3 - 441.0) < 0.5, "PITCH C3: the recording as it is (Hz)", f3, "441 +- 0.5");
        const auto c4 = render(*e, sampler(SR_ONCE, 0, 200, 0), 72, 0.4);
        const double f4 = frequency(c4, 2400, 16800);
        check(std::fabs(f4 - 882.0) < 1.0, "PITCH C4: an octave up (Hz)", f4, "882 +- 1");
    }
    // ONCE: START 0, LENGTH 100 (half of it): 0.25 s of a 0.5 s recording, then silence though the note is held
    {
        auto e = std::make_unique<Engine>();
        e->prepare(kFs, false, kFs);
        e->setSample(recording(24000, 48000.0, [](int i, int) { return 0.5 * std::sin(2.0 * kPi * 300.0 * i / 48000.0); }));
        const auto y = render(*e, sampler(SR_ONCE, 0, 100, 0), 60, 0.5);
        const double during = rms(y, 2400, 10800), after = rms(y, 13200, 24000);
        // (the loudness against an FM operator's sine at the same LEVEL: a recording at 0.5 is about half of it)
        auto f = std::make_unique<Engine>();
        f->prepare(kFs, false, kFs);
        const auto fm = render(*f, { { "ALGO", 0 }, { "OP1.MODE", FM }, { "OP1.LEVEL", 100 }, { "OP1.SUS", 100 } }, 60, 0.3);
        const double ratio = during / std::max(1e-9, rms(fm, 2400, 10800));
        check(std::fabs(ratio - 0.5) < 0.1, "ONCE: as loud as an FM sine at its LEVEL (x, recorded at 0.5)", ratio, "0.5 +- 0.1");
        check(after < 1e-4, "ONCE: silence past the region, the note held (rms)", after, "< 1e-4");
    }
    // REVERSE: a glide rising 200 -> 2000 Hz over 1 s, heard from its end: falling
    {
        auto e = std::make_unique<Engine>();
        e->prepare(kFs, false, kFs);
        e->setSample(recording(48000, 48000.0, [](int i, int) {
            const double t = i / 48000.0;
            return 0.7 * std::sin(2.0 * kPi * (200.0 * t + 900.0 * t * t)); }));   // (200 + 1800 t Hz)
        const auto y = render(*e, sampler(SR_REVERSE, 0, 200, 0), 60, 0.9);
        const double early = frequency(y, 1200, 6000), late = frequency(y, 33600, 38400);
        check(early > 1700.0 && late < 800.0 && late > 0.0, "REVERSE: the rising glide heard falling (Hz at the end)", late,
              "early > 1700, late < 800");
    }
    // XFADE: a loop of 0.1 s of a 97 Hz sine (its seam jumps); crossfaded, the largest step far smaller
    {
        const auto largestStep = [](int xfade) {
            auto e = std::make_unique<Engine>();
            e->prepare(kFs, false, kFs);
            e->setSample(recording(48000, 48000.0, [](int i, int) { return 0.8 * std::sin(2.0 * kPi * 97.0 * i / 48000.0); }));
            // START 50 %, LENGTH 20 of 200 -> 10 % of what is left: 2400 samples, the seam mid-wave
            const auto y = render(*e, sampler(SR_LOOP, 50, 20, xfade), 60, 0.5);
            double big = 0.0;
            for (size_t i = 4801; i < y.size(); ++i) big = std::max(big, (double)std::fabs(y[i] - y[i - 1]));
            return big;
        };
        const double hard = largestStep(0), soft = largestStep(100);
        check(soft < hard * 0.25, "XFADE: the seam's largest step, crossfaded (x of the hard one)", soft / hard, "< 0.25");
    }
    // MODAL: OP2 SAMPLE into OP1 MODAL, STRIKE 0 (only what comes in excites it)
    {
        const auto modal = [](bool withSample) {
            auto e = std::make_unique<Engine>();
            e->prepare(kFs, false, kFs);
            if (withSample)   // a short noise burst, as a hand on a can
                e->setSample(recording(4800, 48000.0, [](int i, int) {
                    unsigned s = 2463534242u + (unsigned)i * 2654435761u;
                    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                    return (s / 4294967295.0 - 0.5) * 1.6 * std::exp(-i / 800.0); }));
            const std::map<std::string, int> v = { { "ALGO", 0 }, { "OP1.MODE", MODAL }, { "OP1.A", 0 }, { "OP1.B", 0 },
                { "OP1.C", 100 }, { "OP1.D", 60 }, { "OP1.LEVEL", 90 }, { "OP1.SUS", 100 }, { "OP1.STRIKE", 0 },
                { "OP2.MODE", SAMPLE }, { "OP2.A", SR_ONCE }, { "OP2.C", 200 }, { "OP2.LEVEL", 100 }, { "OP2.SUS", 100 } };
            const auto y = render(*e, v, 57, 0.8);
            return rms(y, 9600, 38400);                  // (after the burst: the object ringing)
        };
        const double rung = modal(true), quiet = modal(false);
        check(rung > 1e-3 && quiet < 1e-5, "MODAL: excited by the recording (rms; without: silent)", rung, "> 1e-3, < 1e-5 without");
    }
    // HANDOVER: recordings given between blocks (the message thread's side here); the render allocates and frees none
    {
        auto e = std::make_unique<Engine>();
        e->prepare(kFs, false, kFs);
        e->setPatch(Patch::fromValues(sampler(SR_LOOP, 0, 200, 50)));
        std::vector<float> L(256), R(256);
        e->setSample(recording(9600, 48000.0, [](int i, int) { return std::sin(i * 0.05); }));
        e->noteOn(60, 100);
        e->render(L.data(), R.data(), 256);
        long renderAllocs = 0, renderFrees = 0;
        for (int k = 0; k < 40; ++k) {
            if (k % 3 == 0) e->setSample(recording(4800 + k, 48000.0, [](int i, int) { return std::sin(i * 0.02); }));
            if (k % 7 == 0) e->collectSample();
            const long a = allocations, f = frees;
            e->render(L.data(), R.data(), 256);
            renderAllocs += allocations - a;
            renderFrees += frees - f;
        }
        check(renderAllocs == 0 && renderFrees == 0, "HANDOVER: allocations + frees inside render", (double)(renderAllocs + renderFrees), "0");
    }
    std::printf(failures ? "\nFAILURES (%d failed)\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
