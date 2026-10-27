#pragma once

// Mute and solo, saved with the host session (2026-09-23).
//
// Mute and solo live in the Sequencer, not in the Pattern - deliberately,
// because they are performance state: a pattern copied, banked or recalled
// by a Scene must not drag the mutes of the moment along with it. The price
// was that nothing saved them at all. Reopening a project brought every lane
// back unmuted (a restore builds a fresh Sequencer), and on the Kick the
// editor then drew its CC lanes as muted on top of that, which is how the
// gap was found.
//
// So this is a TRAILER, appended after a product's own session state and read
// from the END of it:
//
//     [ product state ... ][ mute bits : u64 LE ][ solo bits : u64 LE ][ "RMS1" ]
//
// Why a trailer and not a new field inside the format:
//   - no format version bump, and no file magic change, for any of the six;
//   - an OLDER Release reading a NEWER session stops where its own format
//     ends and never looks at the trailing 20 bytes (checked: none of the
//     binary readers demands an exhausted stream);
//   - a NEWER build reading an OLDER session finds no magic at the end and
//     leaves mute and solo exactly as the restore left them.
//
// Deliberately JUCE-free, like the rest of ruidocore_seq: bytes in, bytes out.

#include <array>
#include <cstddef>
#include <cstdint>

namespace ruidocore {

struct MuteSoloState {
    static constexpr int kMaxParts = 64;
    static constexpr std::size_t kTrailerSize = 8 + 8 + 4;

    std::uint64_t muted = 0;
    std::uint64_t soloed = 0;

    template <typename IsMuted, typename IsSoloed>
    static MuteSoloState capture(int partCount, IsMuted&& isMuted, IsSoloed&& isSoloed) {
        MuteSoloState s;
        for (int i = 0; i < partCount && i < kMaxParts; ++i) {
            if (isMuted(i))
                s.muted |= std::uint64_t{1} << i;
            if (isSoloed(i))
                s.soloed |= std::uint64_t{1} << i;
        }
        return s;
    }

    bool isMuted(int part) const { return part >= 0 && part < kMaxParts && ((muted >> part) & 1u) != 0; }
    bool isSoloed(int part) const { return part >= 0 && part < kMaxParts && ((soloed >> part) & 1u) != 0; }

    std::array<std::uint8_t, kTrailerSize> encode() const {
        std::array<std::uint8_t, kTrailerSize> out{};
        for (int b = 0; b < 8; ++b) {
            out[static_cast<std::size_t>(b)] = static_cast<std::uint8_t>((muted >> (8 * b)) & 0xffu);
            out[static_cast<std::size_t>(8 + b)] = static_cast<std::uint8_t>((soloed >> (8 * b)) & 0xffu);
        }
        out[16] = 'R';
        out[17] = 'M';
        out[18] = 'S';
        out[19] = '1';
        return out;
    }

    // True only when `data` ENDS with a trailer. Anything else - an older
    // session, a truncated block - leaves `out` untouched and returns false.
    static bool decodeTrailer(const void* data, std::size_t size, MuteSoloState& out) {
        if (data == nullptr || size < kTrailerSize)
            return false;
        const auto* t = static_cast<const std::uint8_t*>(data) + (size - kTrailerSize);
        if (t[16] != 'R' || t[17] != 'M' || t[18] != 'S' || t[19] != '1')
            return false;
        MuteSoloState s;
        for (int b = 0; b < 8; ++b) {
            s.muted |= std::uint64_t{t[b]} << (8 * b);
            s.soloed |= std::uint64_t{t[8 + b]} << (8 * b);
        }
        out = s;
        return true;
    }
};

} // namespace ruidocore
