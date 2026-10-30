// SPDX-License-Identifier: GPL-3.0-only
// Compare 24 macro-oscillator models with patched and unpatched trigger scenarios.
// Initialize zeroed model memory and render 1 s at 48 kHz in 12-sample blocks.
// Write float32 main/aux output and scenario hashes for reference comparison.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#ifdef MODELS_UPSTREAM
#include "plaits/dsp/engine/additive_engine.h"
#include "plaits/dsp/engine/bass_drum_engine.h"
#include "plaits/dsp/engine/chord_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/hi_hat_engine.h"
#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/noise_engine.h"
#include "plaits/dsp/engine/particle_engine.h"
#include "plaits/dsp/engine/snare_drum_engine.h"
#include "plaits/dsp/engine/speech_engine.h"
#include "plaits/dsp/engine/string_engine.h"
#include "plaits/dsp/engine/swarm_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"
#include "plaits/dsp/engine2/chiptune_engine.h"
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine2/six_op_engine.h"
#include "plaits/dsp/engine2/string_machine_engine.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/engine2/wave_terrain_engine.h"
#include "plaits/resources.h"
namespace P = plaits;
namespace S = stmlib;
#else
#include "engines/macro/models/dsp/engine/additive_engine.h"
#include "engines/macro/models/dsp/engine/bass_drum_engine.h"
#include "engines/macro/models/dsp/engine/chord_engine.h"
#include "engines/macro/models/dsp/engine/fm_engine.h"
#include "engines/macro/models/dsp/engine/grain_engine.h"
#include "engines/macro/models/dsp/engine/hi_hat_engine.h"
#include "engines/macro/models/dsp/engine/modal_engine.h"
#include "engines/macro/models/dsp/engine/noise_engine.h"
#include "engines/macro/models/dsp/engine/particle_engine.h"
#include "engines/macro/models/dsp/engine/snare_drum_engine.h"
#include "engines/macro/models/dsp/engine/speech_engine.h"
#include "engines/macro/models/dsp/engine/string_engine.h"
#include "engines/macro/models/dsp/engine/swarm_engine.h"
#include "engines/macro/models/dsp/engine/virtual_analog_engine.h"
#include "engines/macro/models/dsp/engine/waveshaping_engine.h"
#include "engines/macro/models/dsp/engine/wavetable_engine.h"
#include "engines/macro/models/dsp/engine2/chiptune_engine.h"
#include "engines/macro/models/dsp/engine2/phase_distortion_engine.h"
#include "engines/macro/models/dsp/engine2/six_op_engine.h"
#include "engines/macro/models/dsp/engine2/string_machine_engine.h"
#include "engines/macro/models/dsp/engine2/virtual_analog_vcf_engine.h"
#include "engines/macro/models/dsp/engine2/wave_terrain_engine.h"
#include "engines/macro/models/resources.h"
#include "engines/macro/models/resources_fm_banks.h"
namespace P = usina_models;
namespace S = usina_dsputil;
#endif

namespace {
// The models, as globals (as on the module: they are large, and want zeroed memory)
P::VirtualAnalogVCFEngine vaVcf;
P::PhaseDistortionEngine phaseDistortion;
P::SixOpEngine sixOp;
P::WaveTerrainEngine waveTerrain;
P::StringMachineEngine stringMachine;
P::ChiptuneEngine chiptune;
P::VirtualAnalogEngine virtualAnalog;
P::WaveshapingEngine waveshaping;
P::FMEngine fm;
P::GrainEngine grain;
P::AdditiveEngine additive;
P::WavetableEngine wavetable;
P::ChordEngine chord;
P::SpeechEngine speech;
P::SwarmEngine swarm;
P::NoiseEngine noise;
P::ParticleEngine particle;
P::StringEngine string;
P::ModalEngine modal;
P::BassDrumEngine bassDrum;
P::SnareDrumEngine snareDrum;
P::HiHatEngine hiHat;
char shared[16384];

struct Model { const char* name; P::Engine* engine; int bank; };   // Bank: the six-operator models' 0..2, else -1
const Model kModels[24] = {
    { "va vcf", &vaVcf, -1 }, { "phase distortion", &phaseDistortion, -1 }, { "six-op bank 1", &sixOp, 0 },
    { "six-op bank 2", &sixOp, 1 }, { "six-op bank 3", &sixOp, 2 }, { "wave terrain", &waveTerrain, -1 },
    { "string machine", &stringMachine, -1 }, { "chiptune", &chiptune, -1 }, { "virtual analog", &virtualAnalog, -1 },
    { "waveshaping", &waveshaping, -1 }, { "fm", &fm, -1 }, { "grain", &grain, -1 }, { "additive", &additive, -1 },
    { "wavetable", &wavetable, -1 }, { "chord", &chord, -1 }, { "speech", &speech, -1 }, { "swarm", &swarm, -1 },
    { "noise", &noise, -1 }, { "particle", &particle, -1 }, { "string", &string, -1 }, { "modal", &modal, -1 },
    { "bass drum", &bassDrum, -1 }, { "snare drum", &snareDrum, -1 }, { "hi-hat", &hiHat, -1 },
};
} // Namespace

struct ModelsCheckResult { uint64_t hash; std::string name; };

// The scenarios, in order; outDir empty: no raw files written
std::vector<ModelsCheckResult> modelsCheckRun(const std::string& out)
{
    std::vector<ModelsCheckResult> results;
    constexpr int kBlock = 12, kBlocks = 4000;
    int n = 0;
    for (int m = 0; m < 24; ++m) {
        for (int patched = 1; patched >= 0; --patched) {
            const Model& md = kModels[m];
            S::BufferAllocator allocator(shared, sizeof shared);
            md.engine->Init(&allocator);
            md.engine->LoadUserData(md.bank >= 0 ? P::fm_patches_table[md.bank] : nullptr);
            md.engine->Reset();
            std::vector<float> y;
            y.reserve((size_t)kBlocks * kBlock * 2);
            float o[kBlock], a[kBlock];
            for (int b = 0; b < kBlocks; ++b) {
                P::EngineParameters p;
                // Use arithmetic triangles for compiler-independent input. Even periods keep
                // peaks within 0..1 and prevent table-index overflow.
                const auto tri = [](int t, int period) {
                    const int q = t % period;
                    return (float)(q < period / 2 ? q : period - q) / (float)(period / 2);
                };
                p.note = 36.0f + 24.0f * tri(b, 628);
                p.harmonics = tri(b + 100, 482);
                p.timbre = tri(b + 250, 368);
                p.morph = tri(b + 400, 570);
                p.accent = 0.8f;
                if (!patched) p.trigger = P::TRIGGER_UNPATCHED;
                else if (b % 500 == 0) p.trigger = P::TRIGGER_RISING_EDGE | P::TRIGGER_HIGH;
                else p.trigger = b % 500 < 100 ? P::TRIGGER_HIGH : P::TRIGGER_LOW;
                bool enveloped = false;
                md.engine->Render(p, o, a, kBlock, &enveloped);
                for (int i = 0; i < kBlock; ++i) {
                    y.push_back(o[i]);
                    y.push_back(a[i]);
                }
            }
            uint64_t h = 1469598103934665603ull;
            const auto* bytes = reinterpret_cast<const unsigned char*>(y.data());
            for (size_t k = 0; k < y.size() * sizeof(float); ++k) h = (h ^ bytes[k]) * 1099511628211ull;
            if (!out.empty()) {
                const std::string file = out + "/" + std::to_string(n) + ".raw";
                if (FILE* f = std::fopen(file.c_str(), "wb")) {
                    std::fwrite(y.data(), sizeof(float), y.size(), f);
                    std::fclose(f);
                }
            }
            results.push_back({ h, std::string(md.name) + ", trigger " + (patched ? "patched" : "unpatched") });
            ++n;
        }
    }
    return results;
}

#ifndef MODELS_CHECK_NO_MAIN
int main(int argc, char** argv)
{
    const auto r = modelsCheckRun(argc > 1 ? argv[1] : ".");
    for (size_t i = 0; i < r.size(); ++i) std::printf("%d %016llx %s\n", (int)i, (unsigned long long)r[i].hash, r[i].name.c_str());
    return 0;
}
#endif
