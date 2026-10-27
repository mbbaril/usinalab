# SPDX-License-Identifier: GPL-3.0-only
"""The Plaits port: Emilie Gillet's macro-oscillator module code (MIT) - its 24 models, their oscillators, physical
models, drums, speech, FM, effects and tables - copied from the read-only upstream clone and changed only by the
patches listed here, each with its reason. stmlib comes from the shared port (tools/port/port_stmlib.py). The voice
(plaits/dsp/voice.*: the module's own trigger, LPG and output stage) is left out: Usina drives the models itself
(src/engines/fm/FmMacro.h), with the operator's envelope and its own LPG stage. The output
(src/engines/macro/plaits) is generated: never edit it by hand, edit this script and run it again.

    python tools/port/port_plaits.py [UPSTREAM_DIR]      (default D:/upstream: eurorack/ in it)

The six-operator models' patch banks (upstream's resources: "My favorite DX7 patches", the DX7's factory cartridges
and others) are not the author's to license: they are split out of resources.cc into resources_fm_banks.cc, which
only a personal build compiles (CMake USINA_PERSONAL_FM_BANKS, off by default). The public build plays the user's
own SysEx banks. docs/BACKLOG.md, THIRD_PARTY.md.

Every patch must match exactly as many times as it says, or the script stops. Checked by tools/port/check_plaits.sh
(bit for bit against the upstream code, compiled in WSL) and tests/MacroTest.cpp (MSVC against upstream's hashes).
"""
import os
import re
import subprocess
import sys

USINA = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
OUT = os.path.join(USINA, 'src', 'engines', 'macro', 'plaits')

# --- the patches: (file, old, new, times, why). Applied to the upstream text, before the renames below ------------
PATCHES = [
    # the FM banks out of the tables (see above): their declarations and their data
    ('plaits/resources.h',
     'extern const uint8_t* const fm_patches_table[];\n', '', 1,
     'the six-operator banks: not the author\'s to license - resources_fm_banks.h, a personal build only'),
    ('plaits/resources.h',
     'extern const uint8_t syx_bank_0[];\nextern const uint8_t syx_bank_1[];\nextern const uint8_t syx_bank_2[];\n', '', 1,
     '(the same)'),
    # an empty table list: a zero-length array, which GCC accepts and MSVC does not; nothing reads it
    ('plaits/resources.h',
     'extern const int32_t* const lookup_table_i32_table[];\n', '', 1,
     'an empty, unused table list (a zero-length array: MSVC rejects it)'),
    ('plaits/resources.cc',
     'const int32_t* const lookup_table_i32_table[] = {\n};\n', '', 1, '(the same)'),
]

GENERIC = [  # (old, new): every file, after the patches
    ('#include "stmlib/', '#include "port/stmlib/'),
    ('#include "plaits/', '#include "engines/macro/plaits/'),
    ('using namespace stmlib;', 'using namespace usina_stmlib;'),
    ('stmlib::', 'usina_stmlib::'),
    ('namespace plaits {', 'namespace usina_plaits {'),
    ('// namespace plaits', '// namespace usina_plaits'),
    ('plaits::', 'usina_plaits::'),
    ('PLAITS_', 'USINA_PLAITS_'),
]


def git_head(path):
    try:
        return subprocess.check_output(['git', '-C', path, 'rev-parse', '--short', 'HEAD'], text=True).strip()
    except Exception:
        return '?'


def split_banks(text):
    """resources.cc without the banks, and the banks alone (the personal build's file)."""
    start = text.index('const uint8_t syx_bank_0[] = {')
    end = text.index('};', text.index('const uint8_t* const fm_patches_table[] = {')) + 3
    banks = text[start:end]
    rest = text[:start] + text[end:]
    head = text[:text.index('#include')]
    banks_cc = (head + '// (USINA: the six-operator models\' banks alone, split out of resources.cc by the port script. They are\n'
                '// the author\'s favourite DX7 patches - the instrument\'s factory cartridges and others - whose rights are not\n'
                '// the author\'s to grant: compiled only by a personal build, CMake USINA_PERSONAL_FM_BANKS)\n\n'
                '#include "plaits/resources_fm_banks.h"\n\nnamespace plaits {\n\n' + banks + '\n}  // namespace plaits\n')
    banks_h = (head + '#ifndef PLAITS_RESOURCES_FM_BANKS_H_\n#define PLAITS_RESOURCES_FM_BANKS_H_\n\n'
               '#include "stmlib/stmlib.h"\n\nnamespace plaits {\n\n'
               '// (USINA: a personal build only - see resources_fm_banks.cc)\n'
               'extern const uint8_t syx_bank_0[];\nextern const uint8_t syx_bank_1[];\nextern const uint8_t syx_bank_2[];\n'
               'extern const uint8_t* const fm_patches_table[];\n\n'
               '}  // namespace plaits\n\n#endif  // PLAITS_RESOURCES_FM_BANKS_H_\n')
    return rest, banks_cc, banks_h


def main():
    up = sys.argv[1] if len(sys.argv) > 1 else 'D:/upstream'
    euro = os.path.join(up, 'eurorack')
    rev = git_head(euro)
    base = os.path.join(euro, 'plaits')
    files = []
    for root, _, names in os.walk(os.path.join(base, 'dsp')):
        for n in names:
            if n.endswith(('.h', '.cc')):
                rel = os.path.relpath(os.path.join(root, n), base).replace('\\', '/')
                if rel not in ('dsp/voice.h', 'dsp/voice.cc'):
                    files.append(rel)
    files = sorted(files) + ['resources.h', 'resources.cc']
    used = {i: 0 for i in range(len(PATCHES))}
    outputs = {}
    for rel in files:
        with open(os.path.join(base, rel), encoding='utf-8', newline='') as fh:
            text = fh.read().replace('\r\n', '\n')
        key = 'plaits/' + rel
        if rel == 'resources.cc':
            text, banks_cc, banks_h = split_banks(text)
            outputs['resources_fm_banks.cc'] = ('resources.cc (the banks)', banks_cc)
            outputs['resources_fm_banks.h'] = ('resources.h (the banks)', banks_h)
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
        outputs[rel] = (rel, text)
    unused = [i for i, n in used.items() if n == 0]
    if unused:
        sys.exit('patches never applied: %s' % unused)
    for rel, (upath, text) in outputs.items():
        for old, new in GENERIC:
            text = text.replace(old, new)
        for bad in ('Mutable', '__attribute__', '__asm'):
            if bad in text:
                sys.exit('%s still has "%s"' % (rel, bad))
        note = ('// USINA PORT - generated by tools/port/port_plaits.py from eurorack @ %s, plaits/%s. Do not edit: edit\n'
                '// the script (its PATCHES list each change and why). MIT, the upstream notice below; THIRD_PARTY.md.\n'
                % (rev, upath))
        dst = os.path.join(OUT, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'w', encoding='utf-8', newline='\n') as fh:
            fh.write(note + text)
    print('%d files -> %s (eurorack %s)' % (len(outputs), OUT, rev))


if __name__ == '__main__':
    main()
