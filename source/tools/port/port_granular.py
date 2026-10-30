# SPDX-License-Identifier: GPL-3.0-only
"""The granular processor's port (MIT; THIRD_PARTY.md): the 2014 texture-synthesizer module's code as its
alternative firmware extends it (six modes - granular, pitch shifter / time stretcher, looping delay, spectral, and
the two the alternative firmware adds, a reverb with pitch-shifted feedback and a resonator bank) - the processor,
its players, its effects, its phase vocoder and its tables - copied from the read-only upstream clone and changed
only by the patches listed here, each with its reason. The DSP utilities come from the shared port
(port_dsputil.py; the alternative firmware pins its own fork of them, which the shared port does not follow - the
check below compiles upstream against the same utilities, so the two are compared on equal terms). The module's
hardware side (its UI, CV scaling, settings, drivers) is left out: Usina drives the processor itself
(src/fx/Vapor.h). The output (src/fx/granular) is generated: never edit it by hand, edit this script and run it again.

    python tools/port/port_granular.py UPSTREAM_DIR      (the folder holding the upstream clones)

Every patch must match exactly as many times as it says, or the script stops. Checked by tools/port/check_granular.sh
(bit for bit against the upstream code, compiled in WSL) and tests/VaporTest.cpp (MSVC against upstream's hashes).

Run under the address, undefined-behaviour and float-cast sanitizers, the scenarios found four things; three are
patched below (a read past a table, a shift by 32, a float beyond 16 bits), each to what the module's own chip does.
The fourth is left as written: the looping player's signed left shifts that overflow (positions << 4) - defined in
C++20, which the plugin is built with (two's complement: they wrap), and wrapped the same way by every compiler the
checks use.
"""
import os
import re

from portlib import (DSPUTIL_GENERIC, USINA, apply_patches, check_all_used, check_names, git_head, upstream_dir,
                     write)

OUT = os.path.join(USINA, 'src', 'fx', 'granular')
UPSTREAM = ('parasites', 'clouds')  # the upstream clone's folder and the module's folder in it

CORRELATOR_NOTE = '(USINA: a shift by 32 is 0 on the module\'s ARM; undefined in C++; x86 would shift by 0)'

# Patches: source file, old text, new text, count and rationale. Apply before renaming.
PATCHES = [
    ('clouds/dsp/granular_processor.cc', '#include "clouds/drivers/debug_pin.h"\n', '', 1,
     'the module\'s debug pin (a hardware driver; its TIC/TOC are comments here): left out'),
    ('clouds/dsp/random_oscillator.h', '#include "../resources.h"\n', '#include "clouds/resources.h"\n', 1,
     'a relative include, written as every other file writes it'),
    # At control value one, interpolation reads past a four-entry table despite a zero
    # fractional weight. Reuse the final entry without the invalid read.
    ('clouds/dsp/grain.h',
     '    float b = table[index_integral + 1];\n',
     '    float b = table[index_integral < static_cast<int32_t>(size) ? index_integral + 1 : index_integral];\n'
     '    // (USINA: at the top, the last value again - upstream reads past the table there; the same result)\n', 1,
     'an upstream out-of-range read (the 5th value of a 4-value table) at the window shape\'s top: the same result '
     'without it'),
    # A 32-bit shift by 32 is undefined. Explicitly return zero for the next-word
    # contribution when offset_bits is zero, preserving the intended splice score.
    ('clouds/dsp/correlator.cc',
     '    destination_bits |= destination[i + 1] >> (32 - offset_bits);\n',
     '    destination_bits |= offset_bits ? destination[i + 1] >> (32 - offset_bits) : 0u;  // ' + CORRELATOR_NOTE + '\n',
     1, 'a shift by 32, undefined in C++: written with what the module\'s ARM processor does (0)'),
    # Evaluate powf in double and round once to float to reduce platform-dependent
    # last-bit differences. Reference checks use the same expression.
    ('clouds/dsp/resonestor.h',
     '      comb_feedback_[p][voice_] = powf(feedback_[voice_], comb_period_[p][voice_] / 32000.0f);\n',
     '      comb_feedback_[p][voice_] = static_cast<float>(std::pow(static_cast<double>(feedback_[voice_]), '
     'static_cast<double>(comb_period_[p][voice_] / 32000.0f)));\n'
     '      // (USINA: powf in double, rounded once - the same on every platform; upstream\'s powf is each C library\'s)\n',
     1, 'powf, whose last bit is each C library\'s: in double, rounded once - the same on every compiler'),
    # Wrap nonnegative phase advance modulo 65536 through a wider integer.
    # Direct out-of-range float-to-uint16 conversion is undefined.
    ('clouds/dsp/pvoc/frame_transformation.cc',
     '    phases_[i] += static_cast<uint16_t>(\n        static_cast<float>(phases_delta_[i]) * pitch_ratio);\n',
     '    phases_[i] += static_cast<uint16_t>(static_cast<uint32_t>(\n'
     '        static_cast<float>(phases_delta_[i]) * pitch_ratio));  // (USINA: modulo 65536, defined - as both chips do)\n',
     1, 'a float beyond 65535 into 16 bits (undefined in C++): through 32 bits, modulo 65536 - what both chips do'),
    # Optional internal feedback limiter bounds return power against held input power.
    # Apply inside the current block; external feedback control cannot immediately
    # constrain buffered loop energy. Zero attenuation preserves unbounded output
    # below the threshold and matches zero-initialized processor storage.
    ('clouds/dsp/parameters.h',
     '    float warp;\n  } spectral;\n};\n',
     '    float warp;\n  } spectral;\n'
     '  // (USINA: LIMIT - the fed-back signal\'s power ceiling; 0 off. Last: the module fills this struct by position\n'
     '  // in places - a member added in the middle would shift the others)\n'
     '  float usina_fb_ceiling;\n};\n',
     1, 'USINA LIMIT: the feedback ceiling the wrapper sets (at the end: the module fills the struct by position)'),
    ('clouds/dsp/granular_processor.h',
     '  float freeze_lp_;\n',
     '  float freeze_lp_;\n  float usina_fb_atten_;  // (USINA: LIMIT - the attenuation on the fed-back signal, 0 none)\n',
     1, 'USINA LIMIT: its state (an attenuation: zeroed memory means none)'),
    ('clouds/dsp/granular_processor.cc',
     '    fb_filter_[1].Process<FILTER_MODE_HIGH_PASS>(&fb_[0].r, &fb_[0].r, size, 2);\n',
     '    fb_filter_[1].Process<FILTER_MODE_HIGH_PASS>(&fb_[0].r, &fb_[0].r, size, 2);\n'
     '    // (USINA LIMIT: the fed-back signal held within a ceiling set by the wrapper - fx/Vapor.cpp: the\n'
     '    // input\'s held power, +6 dB. Dense grains with feedback grew 25 dB without it. 0: off, the module as it is)\n'
     '    if (parameters_.usina_fb_ceiling > 0.0f) {\n'
     '      float power = 0.0f;\n'
     '      for (size_t i = 0; i < size; ++i) {\n'
     '        power += 0.5f * (fb_[i].l * fb_[i].l + fb_[i].r * fb_[i].r);\n'
     '      }\n'
     '      power /= static_cast<float>(size);\n'
     '      float target = power > parameters_.usina_fb_ceiling\n'
     '          ? 1.0f - sqrtf(parameters_.usina_fb_ceiling / power) : 0.0f;\n'
     '      float from = usina_fb_atten_;\n'
     '      usina_fb_atten_ += (target - usina_fb_atten_) * (target > usina_fb_atten_ ? 0.6f : 0.02f);\n'
     '      if (target == 0.0f && usina_fb_atten_ < 1.0e-5f) usina_fb_atten_ = 0.0f;\n'
     '      if (from != 0.0f || usina_fb_atten_ != 0.0f) {\n'
     '        float step = (usina_fb_atten_ - from) / static_cast<float>(size);\n'
     '        float a = from;\n'
     '        for (size_t i = 0; i < size; ++i) {\n'
     '          a += step;\n'
     '          fb_[i].l *= 1.0f - a;\n'
     '          fb_[i].r *= 1.0f - a;\n'
     '        }\n'
     '      }\n'
     '    } else {\n'
     '      usina_fb_atten_ = 0.0f;\n'
     '    }\n',
     1, 'USINA LIMIT: the fed-back signal held within the ceiling, in the block it is measured'),
]

GENERIC = DSPUTIL_GENERIC + [  # Apply replacements to every file after patches.
    ('#include "clouds/', '#include "fx/granular/'),
    ('namespace clouds {', 'namespace usina_granular {'),
    ('namespace clouds\n{', 'namespace usina_granular\n{'),      # Match the surrounding source formatting.
    ('// namespace clouds', '// namespace usina_granular'),
    ('clouds::', 'usina_granular::'),
    ('CLOUDS_', 'USINA_GRANULAR_'),
]


def main():
    src = os.path.join(upstream_dir('python tools/port/port_granular.py UPSTREAM_DIR'), UPSTREAM[0])
    rev = git_head(src)
    base = os.path.join(src, UPSTREAM[1])
    files = []
    for root, _, names in os.walk(os.path.join(base, 'dsp')):
        for n in names:
            if n.endswith(('.h', '.cc')):
                files.append(os.path.relpath(os.path.join(root, n), base).replace('\\', '/'))
    files = sorted(files) + ['resources.h', 'resources.cc']
    used = {i: 0 for i in range(len(PATCHES))}
    for rel in files:
        with open(os.path.join(base, rel), encoding='utf-8', newline='') as fh:
            text = fh.read().replace('\r\n', '\n')
        text = apply_patches(UPSTREAM[1] + '/' + rel, text, PATCHES, used)
        for old, new in GENERIC:
            text = text.replace(old, new)
        check_names(rel, text)
        write(OUT, rel, 'port_granular.py', text)
    check_all_used(used)
    print('%d files -> %s (upstream %s)' % (len(files), OUT, rev))


if __name__ == '__main__':
    main()
