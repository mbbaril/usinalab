# SPDX-License-Identifier: GPL-3.0-only
"""The 2014 granular port: Emilie Gillet's texture-synthesizer module code (MIT) as Matthias Puech's alternative
firmware extends it (MIT: its six modes - granular, pitch shifter / time stretcher, looping delay, spectral, and the
two it adds, a reverb with pitch-shifted feedback and a resonator bank) - the processor, its players, its effects,
its phase vocoder and its tables - copied from the read-only upstream clone and changed only by the patches listed
here, each with its reason. stmlib comes from the shared port (tools/port/port_stmlib.py; the alternative firmware
pins its own fork of it, which the shared port does not follow - the check below compiles upstream against the same
stmlib, so the two are compared on equal terms). The module's hardware side (its UI, CV scaling, settings, drivers)
is left out: Usina drives the processor itself (src/fx/Vapor.h). The output (src/fx/clouds) is generated: never edit
it by hand, edit this script and run it again.

    python tools/port/port_clouds.py [UPSTREAM_DIR]      (default D:/upstream: parasites/ in it)

Every patch must match exactly as many times as it says, or the script stops. Checked by tools/port/check_clouds.sh
(bit for bit against the upstream code, compiled in WSL) and tests/VaporTest.cpp (MSVC against upstream's hashes).

Run under the address, undefined-behaviour and float-cast sanitizers (22/10), the scenarios found four things; three
are patched below (a read past a table, a shift by 32, a float beyond 16 bits), each to what the module's own chip
does. The fourth is left as written: the looping player's signed left shifts that overflow (positions << 4) - defined
in C++20, which the plugin is built with (two's complement: they wrap), and wrapped the same way by every compiler
the checks use.
"""
import os
import re
import subprocess
import sys

USINA = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
OUT = os.path.join(USINA, 'src', 'fx', 'clouds')

CORRELATOR_NOTE = '(USINA: a shift by 32 is 0 on the module\'s ARM; undefined in C++; x86 would shift by 0)'

# --- the patches: (file, old, new, times, why). Applied to the upstream text, before the renames below ------------
PATCHES = [
    ('clouds/dsp/granular_processor.cc', '#include "clouds/drivers/debug_pin.h"\n', '', 1,
     'the module\'s debug pin (a hardware driver; its TIC/TOC are comments here): left out'),
    ('clouds/dsp/random_oscillator.h', '#include "../resources.h"\n', '#include "clouds/resources.h"\n', 1,
     'a relative include, written as every other file writes it'),
    # an upstream read past a table: the window's shape at its top (1.0: TEXTURE from 0.75 up in the granular mode)
    # indexes slope_response/bias_response (4 values) at 3.0 and reads the 5th value - which the fractional part 0
    # then multiplies by 0, so the module's result is the 4th value whatever follows in memory (as long as it is a
    # finite float). Found 22/10 by the address sanitizer on upstream's own build (tools/port/check_clouds.sh's
    # scenarios). At the top the last value is used again: the same result, without the read
    ('clouds/dsp/grain.h',
     '    float b = table[index_integral + 1];\n',
     '    float b = table[index_integral < static_cast<int32_t>(size) ? index_integral + 1 : index_integral];\n'
     '    // (USINA: at the top, the last value again - upstream reads past the table there; the same result)\n', 1,
     'an upstream out-of-range read (the 5th value of a 4-value table) at the window shape\'s top: the same result '
     'without it'),
    # a 32-bit shift by 32 (offset_bits 0: every 32nd candidate of the splice search): undefined in C++; the module's
    # processor (ARM: a register shift of 32 or more gives 0) takes no bits from the next word, as meant, while an x86
    # build (the count masked to 5 bits: a shift by 0) ORs the whole next word in and scores those candidates wrong.
    # Found 22/10 by the undefined-behaviour sanitizer. Written with the module's meaning; tools/port/check_clouds.sh
    # builds its reference with the same expression, so the reference is the module as its own chip runs it
    ('clouds/dsp/correlator.cc',
     '    destination_bits |= destination[i + 1] >> (32 - offset_bits);\n',
     '    destination_bits |= offset_bits ? destination[i + 1] >> (32 - offset_bits) : 0u;  // ' + CORRELATOR_NOTE + '\n',
     1, 'a shift by 32, undefined in C++: written with what the module\'s ARM processor does (0)'),
    # the one inexact math-library call (the others: roundf, fabs - exact everywhere): powf, whose last bit differs
    # between C libraries (the module's newlib, glibc, MSVC's - measured 22/10: the resonator mode alone differed
    # between g++ and MSVC). Evaluated in double and rounded once to float: correctly rounded on every platform
    # (barring a double rounding, about one call in 2^29). tools/port/check_clouds.sh builds its reference with the
    # same expression
    ('clouds/dsp/resonestor.h',
     '      comb_feedback_[p][voice_] = powf(feedback_[voice_], comb_period_[p][voice_] / 32000.0f);\n',
     '      comb_feedback_[p][voice_] = static_cast<float>(std::pow(static_cast<double>(feedback_[voice_]), '
     'static_cast<double>(comb_period_[p][voice_] / 32000.0f)));\n'
     '      // (USINA: powf in double, rounded once - the same on every platform; upstream\'s powf is each C library\'s)\n',
     1, 'powf, whose last bit is each C library\'s: in double, rounded once - the same on every compiler'),
    # a float beyond 65535 converted to uint16_t (the phase advance of a high partial at a high pitch: measured 22/10,
    # 66498.2, by the float-cast sanitizer): undefined in C++. Both the module's processor (to 32 bits, then the low 16)
    # and x86 (the same, through 32 bits) wrap it - the phase arithmetic meant modulo 65536; the values are never
    # negative (an unsigned delta times a positive ratio). Written with that meaning, defined: the same bits
    ('clouds/dsp/pvoc/frame_transformation.cc',
     '    phases_[i] += static_cast<uint16_t>(\n        static_cast<float>(phases_delta_[i]) * pitch_ratio);\n',
     '    phases_[i] += static_cast<uint16_t>(static_cast<uint32_t>(\n'
     '        static_cast<float>(phases_delta_[i]) * pitch_ratio));  // (USINA: modulo 65536, defined - as both chips do)\n',
     1, 'a float beyond 65535 into 16 bits (undefined in C++): through 32 bits, modulo 65536 - what both chips do'),
]

GENERIC = [  # (old, new): every file, after the patches
    ('#include "stmlib/', '#include "port/stmlib/'),
    ('#include "clouds/', '#include "fx/clouds/'),
    ('using namespace stmlib;', 'using namespace usina_stmlib;'),
    ('stmlib::', 'usina_stmlib::'),
    ('namespace clouds {', 'namespace usina_clouds {'),
    ('namespace clouds\n{', 'namespace usina_clouds\n{'),      # (the alternative firmware's own files' style)
    ('// namespace clouds', '// namespace usina_clouds'),
    ('clouds::', 'usina_clouds::'),
    ('CLOUDS_', 'USINA_CLOUDS_'),
]


def git_head(path):
    try:
        return subprocess.check_output(['git', '-C', path, 'rev-parse', '--short', 'HEAD'], text=True).strip()
    except Exception:
        return '?'


def main():
    up = sys.argv[1] if len(sys.argv) > 1 else 'D:/upstream'
    para = os.path.join(up, 'parasites')
    rev = git_head(para)
    base = os.path.join(para, 'clouds')
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
        key = 'clouds/' + rel
        for i, (f, old, new, times, why) in enumerate(PATCHES):
            if f != key:
                continue
            if isinstance(old, re.Pattern):
                text, n = old.subn(new.replace('\\', '\\\\'), text)
            else:
                n = text.count(old)
                text = text.replace(old, new)
            if n != times:
                sys.exit('patch %d (%s) matched %d times, not %d: %s' % (i, f, n, times, why))
            used[i] += 1
        for old, new in GENERIC:
            text = text.replace(old, new)
        for bad in ('Mutable', '__attribute__', '__asm', 'debug_pin'):
            if bad in text:
                sys.exit('%s still has "%s"' % (rel, bad))
        note = ('// USINA PORT - generated by tools/port/port_clouds.py from parasites @ %s, clouds/%s. Do not edit:\n'
                '// edit the script (its PATCHES list each change and why). MIT, the upstream notice below; THIRD_PARTY.md.\n'
                % (rev, rel))
        dst = os.path.join(OUT, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'w', encoding='utf-8', newline='\n') as fh:
            fh.write(note + text)
    unused = [i for i, n in used.items() if n == 0]
    if unused:
        sys.exit('patches never applied: %s' % unused)
    print('%d files -> %s (parasites %s)' % (len(files), OUT, rev))


if __name__ == '__main__':
    main()
