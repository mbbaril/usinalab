// SPDX-License-Identifier: GPL-3.0-only
#include "FmMacro.h"
#include "FmEngine.h"
#include <cstring>
#include <new>
#ifdef USINA_PERSONAL_FM_BANKS
#include "engines/macro/models/resources_fm_banks.h"
#endif

namespace usina::fm {

namespace {
// the instrument's INIT VOICE, packed (128 bytes, the format models/dsp/fm/patch.h Unpack reads): OP1 a sine at level
// 99, the other five silent, algorithm 1, the envelopes at once; written from the format, no one's patch
std::array<uint8_t, 4096> initBank()
{
    std::array<uint8_t, 128> v {};
    for (int i = 0; i < 6; ++i) {                        // (packed OP6 first: i = 5 is OP1)
        uint8_t* o = &v[(size_t)(i * 17)];
        o[0] = o[1] = o[2] = o[3] = 99;                  // rates
        o[4] = o[5] = o[6] = 99;                         // levels 1..3
        o[7] = 0;                                        // level 4
        o[8] = 39;                                       // break point (C3)
        o[12] = 7 << 3;                                  // detune centred, no rate scaling
        o[14] = i == 5 ? 99 : 0;                         // output level
        o[15] = 1 << 1;                                  // ratio, coarse 1
    }
    for (int j = 0; j < 4; ++j) {
        v[(size_t)(102 + j)] = 99;                       // the pitch envelope: flat
        v[(size_t)(106 + j)] = 50;
    }
    v[110] = 0;                                          // algorithm 1
    v[111] = 1 << 3;                                     // feedback 0, oscillator sync on
    v[112] = 35;                                         // the LFO's speed
    v[116] = (3 << 4) | 1;                               // its sync, the pitch modulation sensitivity 3
    v[117] = 24;                                         // transpose: none (C3)
    const char name[10] = { 'I', 'N', 'I', 'T', ' ', 'V', 'O', 'I', 'C', 'E' };
    for (int i = 0; i < 10; ++i) v[(size_t)(118 + i)] = (uint8_t)name[i];
    std::array<uint8_t, 4096> b {};
    for (int p = 0; p < 32; ++p) std::memcpy(&b[(size_t)(p * 128)], v.data(), 128);
    return b;
}
} // namespace

const uint8_t* macroFmBank(int bank)
{
#ifdef USINA_PERSONAL_FM_BANKS
    return usina_models::fm_patches_table[std::clamp(bank, 0, 2)];
#else
    (void)bank;
    static const std::array<uint8_t, 4096> b = initBank();
    return b.data();
#endif
}

// (the models want zeroed memory - on the module they are globals: made in memory cleared first)
Macro* newMacro()
{
    void* p = ::operator new(sizeof(Macro), std::align_val_t(alignof(Macro)));
    std::memset(p, 0, sizeof(Macro));
    return new (p) Macro;
}
void MacroDelete::operator()(Macro* m) const noexcept
{
    if (m == nullptr) return;
    m->~Macro();
    ::operator delete(m, std::align_val_t(alignof(Macro)));
}

} // namespace usina::fm
