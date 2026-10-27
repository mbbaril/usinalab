// SPDX-License-Identifier: GPL-3.0-only
#include "Delay.h"
#include <algorithm>
#include <cmath>

namespace usina::fx {

namespace {
constexpr double kPi = 3.14159265358979323846;
// FmSchema.h kSyncBeats (the same divisions as the LFOs' SYNC), in beats
constexpr double kSyncBeats[15] = { 0.0, 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5,
                                    2.0, 4.0, 8.0, 16.0 };

// the loop's limiter: unity under 0.8, a smooth knee to a ceiling of 1.25 (FEEDBACK past 100 % grows into it and is
// held there; a loop at 100 % or less never reaches it with a sane input)
double limit(double x)
{
    const double a = std::fabs(x);
    if (a <= 0.8) return x;
    const double over = a - 0.8;
    const double y = 0.8 + 0.45 * std::tanh(over / 0.45);   // (slope 1 at the knee: no corner)
    return x < 0.0 ? -y : y;
}
} // namespace

void Delay::prepare(double sampleRate, double maxSeconds)
{
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxDelay = std::max(1.0, maxSeconds) * fs;
    int len = 1;
    while (len < (int)maxDelay + 8) len <<= 1;
    for (auto& b : line) b.assign((size_t)len, 0.0f);
    mask = len - 1;
    fadeStep = (float)(1.0 / (0.020 * fs));
    glide = 1.0 - std::exp(-1.0 / (0.010 * fs));        // (the knobs: 10 ms)
    tapXStep = 1.0 / (0.050 * fs);                       // (DIGITAL: a new TIME crossfaded over 50 ms)
    tapeGlide = 1.0 - std::exp(-1.0 / (0.150 * fs));     // (TAPE: a new TIME reached over ~150 ms, the pitch bending)
    wowInc = 0.6 / fs;
    flutInc = 6.5 / fs;
    tapeLoss = 1.0 - std::exp(-2.0 * kPi * 7000.0 / fs); // (TAPE: a pass's highs, from 7 kHz)
    reset();
}

void Delay::reset()
{
    for (auto& b : line) std::fill(b.begin(), b.end(), 0.0f);
    w = 0;
    running = false;
    fresh = true;
    fade = 0.0f;
    for (auto* f : { &lcLp, &hcLp, &tapeLp })
        for (auto& p : *f) p.y = 0.0;
    wowPh = flutPh = 0.0;
    lastCharacter = -1;
}

double Delay::delaySamples(const Settings& s) const
{
    double t;
    if (s.sync > 0) {
        const double bpm = std::isfinite(s.bpm) && s.bpm >= 20.0 && s.bpm <= 999.0 ? s.bpm : 120.0;
        t = kSyncBeats[std::clamp(s.sync, 1, 14)] * 60.0 / bpm * fs;
    } else {
        t = std::clamp((double)s.timeMs, 1.0, 4000.0) * 0.001 * fs;
    }
    return std::clamp(t, 3.0, maxDelay - 4.0);           // (the cubic reads two samples ahead: never the slot being
}                                                        // written)

float Delay::read(const std::vector<float>& b, double back) const
{
    const double p = (double)w - back;
    const double fl = std::floor(p);
    const double f = p - fl;
    const int i = (int)fl;
    const float xm1 = b[(size_t)((i - 1) & mask)], x0 = b[(size_t)(i & mask)], x1 = b[(size_t)((i + 1) & mask)],
                x2 = b[(size_t)((i + 2) & mask)];
    // (Hermite: through x0 and x1, the slopes from the neighbours)
    const double c1 = 0.5 * (x1 - xm1), c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
    return (float)(((c3 * f + c2) * f + c1) * f + x0);
}

void Delay::process(float* l, float* r, int n, const Settings& s, bool on)
{
    if (!running) {
        if (!on) return;                                 // (off: untouched, bit for bit)
        running = true;
        fade = 0.0f;
    }
    const double target = delaySamples(s);
    const int ch = std::clamp(s.character, 0, (int)kNumCharacters - 1);
    const int route = std::clamp(s.routing, 0, (int)kNumRoutings - 1);
    const bool tape = ch == kTape;
    if (fresh || ch != lastCharacter) {                  // (from rest, or a new character: the time where it is)
        tapA = tapB = tapeTime = target;
        tapX = 1.0;
        lastCharacter = ch;
    }
    const double fbTo = std::clamp((double)s.feedback, 0.0, 1.1);
    const double mixTo = std::clamp((double)s.mix, 0.0, 1.0);
    const double outTo = std::pow(10.0, std::clamp((double)s.outputDb, -24.0, 12.0) / 20.0);
    const double frzTo = s.freeze ? 1.0 : 0.0;
    const auto cutK = [&](double hz) { return 1.0 - std::exp(-2.0 * kPi * std::clamp(hz, 1.0, 0.45 * fs) / fs); };
    const double lcTo = s.lowCutHz <= 20.5f ? 0.0 : cutK(s.lowCutHz);           // (a one-pole low-pass taken out)
    const double hcTo = s.highCutHz >= 19999.0f ? 1.0 : cutK(s.highCutHz);
    const double wowTo = tape ? std::clamp((double)s.wow, 0.0, 1.0) : 0.0;
    const double flutTo = tape ? std::clamp((double)s.flutter, 0.0, 1.0) : 0.0;
    if (fresh) {
        flutDepth = flutTo;
        fb = fbTo;
        mix = mixTo;
        out = outTo;
        freezeShare = frzTo;
        lcK = lcTo;
        hcK = hcTo;
        wowDepth = wowTo;
        fresh = false;
    }
    const float fadeTo = on ? 1.0f : 0.0f;
    for (int i = 0; i < n; ++i) {
        fb += glide * (fbTo - fb);
        mix += glide * (mixTo - mix);
        out += glide * (outTo - out);
        freezeShare += glide * (frzTo - freezeShare);
        lcK += glide * (lcTo - lcK);
        hcK += glide * (hcTo - hcK);
        wowDepth += glide * (wowTo - wowDepth);
        flutDepth += glide * (flutTo - flutDepth);
        fade = fadeTo > fade ? std::min(fadeTo, fade + fadeStep) : std::max(fadeTo, fade - fadeStep);
        // the time: DIGITAL crossfades to a new one (one fade at a time: the next waits for it to end); TAPE glides
        double back;
        if (tape) {
            tapeTime += tapeGlide * (target - tapeTime);
            wowPh += wowInc;
            flutPh += flutInc;
            wowPh -= std::floor(wowPh);
            flutPh -= std::floor(flutPh);
            const double mod = fs * (wowDepth * 0.0025 * std::sin(2.0 * kPi * wowPh) +
                                     flutDepth * 0.0006 * std::sin(2.0 * kPi * flutPh));
            back = std::clamp(tapeTime + mod, 3.0, maxDelay - 4.0);
        } else {
            if (tapX >= 1.0 && std::fabs(target - tapB) > 0.5) {
                tapA = tapB;
                tapB = target;
                tapX = 0.0;
            }
            back = tapB;
        }
        float y[2];
        const int lines = route == kMono ? 1 : 2;
        for (int c = 0; c < lines; ++c) {
            const auto& b = line[(size_t)c];
            if (!tape && tapX < 1.0) {
                const float a = read(b, tapA), bb = read(b, tapB);
                y[c] = a + (bb - a) * (float)tapX;
            } else {
                y[c] = read(b, back);
            }
        }
        if (!tape && tapX < 1.0) tapX = std::min(1.0, tapX + tapXStep);
        if (route == kMono) y[1] = y[0];
        // the loop: what goes back in - the echo through the loop's filters (and TAPE's loss and saturation), times
        // FEEDBACK; held (FREEZE): the echo itself, the input shut
        const double inL = l[i], inR = r[i];
        double send[2];
        for (int c = 0; c < 2; ++c) {
            double e = y[c];
            const double lc = lcLp[(size_t)c].lp(e, lcK);
            if (lcK > 0.0) e -= lc;
            const double hc = hcLp[(size_t)c].lp(e, hcK);
            e = hc;
            if (tape) e = std::tanh(tapeLp[(size_t)c].lp(e, tapeLoss) * 1.2) / 1.2;
            const double kept = (double)y[c];
            send[c] = (e * fb) * (1.0 - freezeShare) + kept * freezeShare;
        }
        const double inGain = 1.0 - freezeShare;
        double wl, wr;
        if (route == kPingPong) {
            wl = 0.5 * (inL + inR) * inGain + send[1];   // (the input into the left; each line feeds the other)
            wr = send[0];
        } else if (route == kMono) {
            wl = wr = 0.5 * (inL + inR) * inGain + send[0];
        } else {
            wl = inL * inGain + send[0];
            wr = inR * inGain + send[1];
        }
        line[0][(size_t)w] = (float)limit(std::isfinite(wl) ? wl : 0.0);
        line[1][(size_t)w] = (float)limit(std::isfinite(wr) ? wr : 0.0);
        w = (w + 1) & mask;
        // the output: the dry and the echoes, faded with the switch
        const double g = (double)fade;
        l[i] = (float)(inL + g * ((inL * (1.0 - mix) + y[0] * mix) * out - inL));
        r[i] = (float)(inR + g * ((inR * (1.0 - mix) + y[1] * mix) * out - inR));
    }
    if (!on && fade <= 0.0f) reset();                    // (faded out: emptied, and off)
}

const char* Delay::characterName(int c)
{
    static const char* const kNames[kNumCharacters] = { "DIGITAL", "TAPE" };
    return kNames[std::clamp(c, 0, (int)kNumCharacters - 1)];
}

const char* Delay::routingName(int r)
{
    static const char* const kNames[kNumRoutings] = { "STEREO", "PING-PONG", "MONO" };
    return kNames[std::clamp(r, 0, (int)kNumRoutings - 1)];
}

} // namespace usina::fx
