# SPDX-License-Identifier: GPL-3.0-only
"""The shared DSP utilities port (MIT; THIRD_PARTY.md): the parts the other ports use - the texture
(port_texture.py), the granular processor (port_granular.py) and the models (port_models.py: the MACRO operator, the
voice's LPG) - copied from the read-only upstream clone and changed only by the patches listed here, each with its
reason. One copy for all of them (two would define the same tables twice). The output (src/port/dsputil) is
generated: never edit it by hand, edit this script and run it again.

    python tools/port/port_dsputil.py UPSTREAM_DIR      (the folder holding the upstream clones)

Every patch must match exactly as many times as it says, or the script stops: an upstream change never slips through
silently. Checked by the ports that use it (tools/port/check_texture.sh, check_lpg.sh, check_models.sh).
"""
import os
import re
import sys

from portlib import (DSPUTIL_GENERIC, USINA, apply_patches, check_all_used, check_names, git_head, upstream_dir,
                     write)

OUT = os.path.join(USINA, 'src', 'port', 'dsputil')
UPSTREAM = 'stmlib'  # the upstream clone's folder

UPSTREAM_FILES = [
    'stmlib.h',
    'dsp/dsp.h', 'dsp/filter.h', 'dsp/units.h', 'dsp/units.cc', 'dsp/parameter_interpolator.h',
    'dsp/cosine_oscillator.h', 'dsp/sample_rate_converter.h', 'dsp/hysteresis_quantizer.h',
    'dsp/hysteresis_filter.h', 'dsp/delay_line.h', 'dsp/limiter.h', 'dsp/polyblep.h', 'dsp/rsqrt.h',
    'utils/random.h', 'utils/random.cc', 'utils/gate_flags.h', 'utils/buffer_allocator.h', 'utils/dsp.h',
    'dsp/atan.h', 'dsp/atan.cc', 'fft/shy_fft.h',                     # Required by spectral granular processing.
]

# Patches: source file, old text, new text, count and rationale. Apply before renaming.
PATCHES = [
    # stmlib.h
    ('stmlib/stmlib.h',
     '#ifndef TEST\n#define IN_RAM __attribute__ ((section (".ramtext")))\n#else\n#define IN_RAM\n#endif  // TEST\n',
     '#define IN_RAM  // (USINA: no RAM sections on a computer - upstream\'s own TEST definition)\n', 1,
     'GCC section attributes: the code runs on a computer, as upstream\'s TEST build'),
    ('stmlib/stmlib.h',
     '#include <stddef.h>\n',
     '#include <stddef.h>\n\n#ifndef M_PI  // (USINA: MSVC defines it only on request; the same value)\n'
     '#define M_PI 3.14159265358979323846\n#endif\n', 1,
     'M_PI, which MSVC\'s <cmath> defines only with _USE_MATH_DEFINES'),
    # dsp.h: the portable versions (upstream's TEST build) instead of ARM assembly
    ('stmlib/dsp/dsp.h',
     '#ifdef TEST\n  inline int32_t Clip16(int32_t x) {',
     '  inline int32_t Clip16(int32_t x) {', 1,
     'Clip16/ClipU16: upstream\'s portable (TEST) versions; the ARM ssat/usat ones removed'),
    ('stmlib/dsp/dsp.h',
     '#else\n  inline int32_t Clip16(int32_t x) {\n    int32_t result;\n'
     '    __asm ("ssat %0, %1, %2" : "=r" (result) :  "I" (16), "r" (x) );\n    return result;\n  }\n'
     '  inline uint32_t ClipU16(int32_t x) {\n    uint32_t result;\n'
     '    __asm ("usat %0, %1, %2" : "=r" (result) :  "I" (16), "r" (x) );\n    return result;\n  }\n#endif\n',
     '', 1, '(the same)'),
    ('stmlib/dsp/dsp.h',
     '#ifdef TEST\n  inline float Sqrt(float x) {\n    return sqrtf(x);\n  }\n#else\n  inline float Sqrt(float x) {\n'
     '    float result;\n    __asm ("vsqrt.f32 %0, %1" : "=w" (result) : "w" (x) );\n    return result;\n  }\n#endif\n',
     '  inline float Sqrt(float x) {\n    return sqrtf(x);\n  }\n', 1,
     'Sqrt: upstream\'s portable (TEST) version; the ARM vsqrt one removed (both are the correctly rounded root)'),
    ('stmlib/dsp/hysteresis_quantizer.h',
     '// Note: currently refactoring this aspect of all Mutable Instruments modules.\n',
     '// Note: currently refactoring this aspect of all the author\'s modules.\n', 1,
     'a brand name in a comment (Usina names no brands)'),
    # Use one random generator per processor instance.
    ('stmlib/utils/random.h',
     '  static inline uint32_t state() { return rng_state_; }\n\n  static inline void Seed(uint32_t seed) {\n'
     '    rng_state_ = seed;\n  }\n\n  static inline uint32_t GetWord() {\n'
     '    rng_state_ = rng_state_ * 1664525L + 1013904223L;\n    return state();\n  }\n',
     '  static inline uint32_t state() { return rng_state(); }\n\n  static inline void Seed(uint32_t seed) {\n'
     '    rng_state() = seed;\n  }\n\n  static inline uint32_t GetWord() {\n'
     '    rng_state() = rng_state() * 1664525L + 1013904223L;\n    return state();\n  }\n\n'
     '  // (USINA: the state is the current processor\'s - set by a Scope on its thread while it runs -, so two\n'
     '  // plugin instances never share one generator nor race on it; without a Scope, a per-thread one)\n'
     '  class Scope {\n   public:\n    explicit Scope(uint32_t* state) : previous_(current_) { current_ = state; }\n'
     '    ~Scope() { current_ = previous_; }\n   private:\n    uint32_t* previous_;\n  };\n', 1,
     'the generator\'s state per processor instance (upstream: one global, shared by all plugin instances and threads)'),
    ('stmlib/utils/random.h',
     ' private:\n  static uint32_t rng_state_;\n',
     ' private:\n  static inline uint32_t& rng_state() { return current_ ? *current_ : default_state_; }\n'
     '  static thread_local uint32_t* current_;\n  static thread_local uint32_t default_state_;\n', 1, '(the same)'),
    ('stmlib/utils/random.cc',
     'uint32_t Random::rng_state_ = 0x21;\n',
     'thread_local uint32_t* Random::current_ = NULL;\nthread_local uint32_t Random::default_state_ = 0x21;\n', 1,
     '(the same; the seed stays upstream\'s 0x21)'),
    # utils/dsp.h: GCC's always_inline on the declarations (the definitions follow; inline either way)
    ('stmlib/utils/dsp.h',
     re.compile(r'\n  __attribute__\(\(always_inline\)\);'), ';', 11,
     'a GCC attribute on forward declarations: the same functions, inline, without it'),
]

def main():
    src = os.path.join(upstream_dir('python tools/port/port_dsputil.py UPSTREAM_DIR'), UPSTREAM)
    rev = git_head(src)
    used = {i: 0 for i in range(len(PATCHES))}
    for f in UPSTREAM_FILES:
        with open(os.path.join(src, f), encoding='utf-8', newline='') as fh:
            text = fh.read().replace('\r\n', '\n')
        text = apply_patches(UPSTREAM + '/' + f, text, PATCHES, used)
        for old, new in DSPUTIL_GENERIC:
            text = text.replace(old, new)
        rel = 'dsputil.h' if f == 'stmlib.h' else f
        check_names(rel, text)
        write(OUT, rel, 'port_dsputil.py', text)
    check_all_used(used)
    print('%d files -> %s (upstream %s)' % (len(UPSTREAM_FILES), OUT, rev))


if __name__ == '__main__':
    main()
