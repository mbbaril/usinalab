// SPDX-License-Identifier: GPL-3.0-only
// Run six granular modes at four quality settings with swept controls, gates and freeze.
// Use zeroed storage and 32-sample blocks at 32 kHz. Build against reference or adapted DSP.
// Write int16 stereo raw output and scenario hashes for sample-exact comparison.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#ifdef GRANULAR_UPSTREAM
#include "clouds/dsp/granular_processor.h"
#include "stmlib/utils/random.h"
namespace C = clouds;
namespace S = stmlib;
#else
#include "fx/granular/dsp/granular_processor.h"
#include "port/dsputil/utils/random.h"
namespace C = usina_granular;
namespace S = usina_dsputil;
#endif

namespace {
// Zero-initialized DSP state and memory regions.
uint8_t blockMem[118784];
uint8_t blockCcm[65536 - 128];
alignas(16) unsigned char processorMem[sizeof(C::GranularProcessor)];

const char* const kModeNames[6] = { "granular", "stretch", "looping delay", "spectral", "reverb", "resonator" };
const char* const kQualityNames[4] = { "stereo 16-bit", "mono 16-bit", "stereo 8-bit", "mono 8-bit" };
} // Namespace

struct GranularCheckResult { uint64_t hash; std::string name; };

// The scenarios, in order; out empty: no raw files written
std::vector<GranularCheckResult> granularCheckRun(const std::string& out)
{
    std::vector<GranularCheckResult> results;
    constexpr int kBlock = 32, kBlocks = 2000;
    // Use arithmetic triangles and seeded noise for compiler-independent input samples.
    // Even periods keep peaks exactly at zero and one.
    const auto tri = [](int t, int period) {
        const int q = t % period;
        return (float)(q < period / 2 ? q : period - q) / (float)(period / 2);
    };
    int n = 0;
    for (int mode = 0; mode < 6; ++mode) {
        for (int quality = 0; quality < 4; ++quality) {
            std::memset(blockMem, 0, sizeof blockMem);
            std::memset(blockCcm, 0, sizeof blockCcm);
            std::memset(processorMem, 0, sizeof processorMem);
            auto* p = new (processorMem) C::GranularProcessor();
            S::Random::Seed(0x21);                       // The generator's power-on state, for each scenario
            p->Init(blockMem, sizeof blockMem, blockCcm, sizeof blockCcm);
            p->set_playback_mode((C::PlaybackMode)mode);
            p->set_quality(quality);
            std::vector<int16_t> y;
            y.reserve((size_t)kBlocks * kBlock * 2);
            C::ShortFrame in[kBlock], o[kBlock];
            uint32_t seed = 1;
            for (int b = 0; b < kBlocks; ++b) {
                for (int i = 0; i < kBlock; ++i) {
                    const int t = b * kBlock + i;
                    seed = seed * 1664525u + 1013904223u;
                    const float noise = (float)(int32_t)(seed >> 16) / 65536.0f - 0.5f;   // -0.5..0.5
                    const float a = tri(t, 146) * 2.0f - 1.0f, c = tri(t, 98) * 2.0f - 1.0f;
                    const float env = tri(t, 16000);              // A slow swell: silences and peaks
                    in[i].l = (short)(12000.0f * env * (0.7f * a + 0.3f * noise));
                    in[i].r = (short)(12000.0f * env * (0.7f * c - 0.3f * noise));
                }
                // Clamp controls below one to avoid out-of-bounds table lookup.
                constexpr float kTop = 65535.0f / 65536.0f;
                C::Parameters* k = p->mutable_parameters();
                k->position = kTop * tri(b, 700);
                k->size = kTop * tri(b + 90, 520);
                k->pitch = -12.0f + 24.0f * tri(b + 200, 900);
                k->density = kTop * tri(b + 40, 640);
                k->texture = kTop * tri(b + 300, 460);
                k->dry_wet = kTop * (0.3f + 0.7f * tri(b + 10, 1200));
                k->stereo_spread = kTop * tri(b + 150, 380);
                k->feedback = 0.8f * tri(b + 500, 760);
                k->reverb = kTop * tri(b + 250, 1000);
                k->freeze = b >= 1100 && b < 1400;
                k->trigger = b % 250 == 0;
                k->gate = b % 250 < 60;
                p->Prepare();                            // The module's main loop, between the interrupts
                p->Process(in, o, kBlock);
                for (int i = 0; i < kBlock; ++i) {
                    y.push_back(o[i].l);
                    y.push_back(o[i].r);
                }
            }
            p->~GranularProcessor();
            uint64_t h = 1469598103934665603ull;
            const auto* bytes = reinterpret_cast<const unsigned char*>(y.data());
            for (size_t j = 0; j < y.size() * sizeof(int16_t); ++j) h = (h ^ bytes[j]) * 1099511628211ull;
            if (!out.empty()) {
                const std::string file = out + "/" + std::to_string(n) + ".raw";
                if (FILE* f = std::fopen(file.c_str(), "wb")) {
                    std::fwrite(y.data(), sizeof(int16_t), y.size(), f);
                    std::fclose(f);
                }
            }
            results.push_back({ h, std::string(kModeNames[mode]) + ", " + kQualityNames[quality] });
            ++n;
        }
    }
    return results;
}

#ifndef GRANULAR_CHECK_NO_MAIN
int main(int argc, char** argv)
{
    const auto r = granularCheckRun(argc > 1 ? argv[1] : ".");
    for (size_t i = 0; i < r.size(); ++i) std::printf("%d %016llx %s\n", (int)i, (unsigned long long)r[i].hash, r[i].name.c_str());
    return 0;
}
#endif
