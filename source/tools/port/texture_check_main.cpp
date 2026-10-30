// SPDX-License-Identifier: GPL-3.0-only
// Tools/port/check_texture.sh's program: built once against upstream (TX_UPSTREAM) and once against the port; prints
// each scenario's hash and writes its output (raw floats) to <dir>/<n>.raw for a sample-by-sample comparison.
#ifdef TX_UPSTREAM
#include "beads/dsp/processor.h"
#include "beads/gain_control.h"
#else
#include "fx/texture/dsp/processor.h"
#include "fx/texture/gain_control.h"
#endif
#include "texture_scenarios.h"
#include <cstdlib>
#include <string>

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    for (int s = 0; s < texture_scenarios::kScenarios; ++s) {
        std::vector<float> out;
        const uint64_t h = texture_scenarios::run(s, &out);
        std::printf("%d %016llx %s\n", s, (unsigned long long)h, texture_scenarios::scenarioName(s));
        if (FILE* f = std::fopen((dir + "/" + std::to_string(s) + ".raw").c_str(), "wb")) {
            std::fwrite(out.data(), sizeof(float), out.size(), f);
            std::fclose(f);
        }
    }
    return 0;
}
