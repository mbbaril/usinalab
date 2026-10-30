// SPDX-License-Identifier: GPL-3.0-only
#include "fx/Filter.h"
#include <algorithm>
#include <cmath>

namespace usina::fx {

namespace {
constexpr double kIn = 2.0;  // 0 dBFS -> an operator's full level, so drive and saturation match the voice path
}

void Filter::prepare(double sampleRate)
{
    fs = sampleRate;
    fl.prepare(fs, false);
    fr.prepare(fs, false);
    glide = 1.0 - std::exp(-1.0 / (0.02 * fs));
    envUp = 1.0 - std::exp(-1.0 / (0.005 * fs));
    envDown = 1.0 - std::exp(-1.0 / (0.12 * fs));
    fadeStep = (float)(1.0 / (0.02 * fs));
    reset();
}

void Filter::reset()
{
    fl.reset();
    fr.reset();
    running = false;
    fresh = true;
    fade = 0.0f;
    env = 0.0;
}

const char* Filter::typeName(int type)
{
    static const char* N[kNumTypes] = { "LPF 12", "LPF 24", "LPF SK", "LPF OTA", "HPF 12", "HPF 24", "HPF SK",
                                        "BPF 6", "BPF 12", "BRF 6", "BRF 12", "SK LPF HOT", "SK HPF HOT", "CMOS LP",
                                        "CMOS HP", "CMOS BP", "CMOS DIST" };
    return N[std::clamp(type, 0, kNumTypes - 1)];
}

void Filter::process(float* l, float* r, int n, const Settings& s, bool on)
{
    if (!running) {
        if (!on) return;
        running = true;
        fade = 0.0f;
    }
    const int type = voiceType(std::clamp(s.type, 0, kNumTypes - 1));
    const double lgTo = std::log2(std::clamp((double)s.cutoffHz, 20.0, 20000.0));
    const double spreadTo = std::clamp((double)s.spread, -1.0, 1.0);
    const double mixTo = std::clamp((double)s.mix, 0.0, 1.0);
    const double outTo = std::pow(10.0, std::clamp((double)s.outputDb, -24.0, 12.0) / 20.0);
    const double resoTo = 100.0 * std::clamp((double)s.reso, 0.0, 1.0);
    const double driveTo = std::exp2(3.0 * std::clamp((double)s.drive, 0.0, 1.0));
    const double followTo = 4.0 * std::clamp((double)s.follow, -1.0, 1.0);
    if (fresh) {
        lg = lgTo;
        spread = spreadTo;
        mix = mixTo;
        out = outTo;
        reso = resoTo;
        drive = driveTo;
        follow = followTo;
        fresh = false;
    }
    const float fadeTo = on ? 1.0f : 0.0f;
    for (int i = 0; i < n; ++i) {
        lg += glide * (lgTo - lg);
        spread += glide * (spreadTo - spread);
        mix += glide * (mixTo - mix);
        out += glide * (outTo - out);
        reso += glide * (resoTo - reso);
        drive += glide * (driveTo - drive);
        follow += glide * (followTo - follow);

        const double x0 = l[i], x1 = r[i];
        const double level = std::min(1.0, 0.5 * (std::fabs(x0) + std::fabs(x1)));
        env += (level > env ? envUp : envDown) * (level - env);

        // Follower depth on a dB scale: -48 dBFS and below -> 0, 0 dBFS -> 1.
        const double depth = env > 0.003981 ? std::min(1.0, 1.0 + 20.0 * std::log10(env) / 48.0) : 0.0;
        const double c = lg + follow * depth;  // cutoff in log2 Hz
        const double y0 = fl.process(kIn * x0, c - spread, type, reso, drive, 0) / kIn;
        const double y1 = fr.process(kIn * x1, c + spread, type, reso, drive, 0) / kIn;

        if (fade != fadeTo)
            fade = fadeTo > fade ? std::min(fadeTo, fade + fadeStep) : std::max(fadeTo, fade - fadeStep);
        const double wet = mix * fade, gain = 1.0 + fade * (out - 1.0);
        l[i] = (float)((x0 + wet * (y0 - x0)) * gain);
        r[i] = (float)((x1 + wet * (y1 - x1)) * gain);
    }
    if (!on && fade <= 0.0f) reset();
}

} // namespace usina::fx
