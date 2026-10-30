// SPDX-License-Identifier: GPL-3.0-only
// Compare reference and adapted LPG output at 48 kHz.
// Update envelopes once per 12-sample block and gates per sample.
// Write float32 raw output and scenario hashes.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "lpg_scenarios.h"

#ifdef LPG_UPSTREAM
#include "plaits/dsp/dsp.h"
#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/envelope.h"
#include "plaits/dsp/fx/low_pass_gate.h"
#include "stmlib/dsp/units.h"
#else
#include "engines/fm/FmLpg.h"
#endif

int main(int argc, char** argv)
{
    using namespace lpgcheck;
    const std::string out = argc > 1 ? argv[1] : ".";
    int n = 0;
    for (const auto& sc : kScenarios) {
        std::vector<float> y = input();
        const int len = kLength;
#ifdef LPG_UPSTREAM
        plaits::LowPassGate lpg;
        plaits::LPGEnvelope env;
        lpg.Init();
        env.Init();
        for (int b = 0; b < len; b += (int)plaits::kBlockSize) {
            if (sc.mode == 1 && b % sc.strikeEvery == 0) env.Trigger();
            const float shortDecay = (200.0f * plaits::kBlockSize) / plaits::kSampleRate *
                                     stmlib::SemitonesToRatio(-96.0f * sc.decay);
            const float hf = sc.color;
            const float decayTail = (20.0f * plaits::kBlockSize) / plaits::kSampleRate *
                                        stmlib::SemitonesToRatio(-72.0f * sc.decay + 12.0f * hf) - shortDecay;
            if (sc.mode == 2) {
                const float level = eg2At(b);
                const float compressed = 1.3f * level / (0.3f + fabsf(level));
                env.ProcessLP(compressed, shortDecay, decayTail, hf);
            } else {
                const float attack = plaits::NoteToFrequency(sc.note) * float(plaits::kBlockSize) * 2.0f;
                env.ProcessPing(attack, shortDecay, decayTail, hf);
            }
            lpg.Process(env.gain(), env.frequency(), env.hf_bleed(), y.data() + b, plaits::kBlockSize);
        }
#else
        usina::fm::VoiceLpg lpg;
        lpg.prepare(48000.0);
        for (int i = 0; i < len; ++i) {
            if (sc.mode == 1 && i % sc.strikeEvery == 0) lpg.trigger();
            const int blockStart = i - i % 12;           // EG2 as the module reads it: once a block, at its start
            y[(size_t)i] = lpg.process(y[(size_t)i], sc.mode, sc.decay, sc.color, eg2At(blockStart), sc.note);
        }
#endif
        const std::string file = out + "/" + std::to_string(n) + ".raw";
        if (FILE* f = std::fopen(file.c_str(), "wb")) {
            std::fwrite(y.data(), sizeof(float), y.size(), f);
            std::fclose(f);
        }
        std::printf("%d %016llx %s\n", n, (unsigned long long)hash(y), sc.name);
        ++n;
    }
    return 0;
}
