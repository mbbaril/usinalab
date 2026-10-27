// SPDX-License-Identifier: GPL-3.0-only
// Export the editor's own algorithm links. This tool never runs on the audio thread.
#include "engines/fm/FmEngine.h"
#include <iostream>
int main() {
    std::cout << "[";
    for (int a = 0; a < usina::fm::kNumAlgs; ++a) {
        if (a) std::cout << ',';
        float links[6][6]; unsigned carriers;
        std::cout << "{\"states\":[";
        for (int state = 0; state < 2; ++state) {
            if (state) std::cout << ',';
            usina::fm::Engine::algorithmLinks(a, state * 100, links, carriers);
            std::cout << "{\"carriers\":" << carriers << ",\"links\":[";
            for (int f = 0; f < 6; ++f) for (int t = 0; t < 6; ++t) {
                if (f || t) std::cout << ',';
                std::cout << links[f][t];
            }
            std::cout << "]}";
        }
        std::cout << "]}";
    }
    std::cout << "]";
}
