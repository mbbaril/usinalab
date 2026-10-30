// SPDX-License-Identifier: GPL-3.0-only
// Minimal codec frame type required to compile reference gain-control DSP on a host.
#ifndef BEADS_DRIVERS_CODEC_H_
#define BEADS_DRIVERS_CODEC_H_
#include <cstdint>
namespace beads {
class Codec {
 public:
  struct Frame {
    int32_t l;
    int32_t r;
  };
};
}  // Namespace beads
#endif  // BEADS_DRIVERS_CODEC_H_
