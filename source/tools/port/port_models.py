# SPDX-License-Identifier: GPL-3.0-only
"""The macro-oscillator models' port (MIT; THIRD_PARTY.md): the 24 models, their oscillators, physical models,
drums, speech, FM, effects and tables - copied from the read-only upstream clone and changed only by the patches
listed here, each with its reason. The DSP utilities come from the shared port (port_dsputil.py). The module's voice
(dsp/voice.*: its own trigger, LPG and output stage) is left out: Usina drives the models itself
(src/engines/fm/FmMacro.h), with the operator's envelope and its own LPG stage. The output (src/engines/macro/models)
is generated: never edit it by hand, edit this script and run it again.

    python tools/port/port_models.py UPSTREAM_DIR      (the folder holding the upstream clones)

The six-operator models' patch banks (upstream's resources) are not the author's to license: they are split out of
resources.cc into resources_fm_banks.cc, which only a personal build compiles (CMake USINA_PERSONAL_FM_BANKS, off by
default). The public build plays the user's own SysEx banks. THIRD_PARTY.md.

Every patch must match exactly as many times as it says, or the script stops. Checked by tools/port/check_models.sh
(bit for bit against the upstream code, compiled in WSL) and tests/MacroTest.cpp (MSVC against upstream's hashes).
"""
import os

from portlib import (DSPUTIL_GENERIC, USINA, apply_patches, check_all_used, check_names, git_head, upstream_dir,
                     write)

OUT = os.path.join(USINA, 'src', 'engines', 'macro', 'models')
UPSTREAM = ('eurorack', 'plaits')  # the upstream clone's folder and the module's folder in it
KEY = 'plaits/'


def neutral(rel, old, new, times=1):
    """A comment naming a product, a module or a person, said without the name."""
    return (KEY + rel, old, new, times, 'a product or person named in a comment (Usina names none)')


# Patches: source file, old text, new text, count and rationale. Apply before renaming.
PATCHES = [
    # Remove excluded FM bank declarations and data from resource tables.
    ('plaits/resources.h',
     'extern const uint8_t* const fm_patches_table[];\n', '', 1,
     'the six-operator banks: not the author\'s to license - resources_fm_banks.h, a personal build only'),
    ('plaits/resources.h',
     'extern const uint8_t syx_bank_0[];\nextern const uint8_t syx_bank_1[];\nextern const uint8_t syx_bank_2[];\n', '', 1,
     '(the same)'),
    # Replace an unused zero-length array with a portable declaration.
    ('plaits/resources.h',
     'extern const int32_t* const lookup_table_i32_table[];\n', '', 1,
     'an empty, unused table list (a zero-length array: MSVC rejects it)'),
    ('plaits/resources.cc',
     'const int32_t* const lookup_table_i32_table[] = {\n};\n', '', 1, '(the same)'),
    neutral('dsp/physical_modelling/delay_line.h', '(same implementation as from stmlib,',
            '(same implementation as the shared utilities\','),
    neutral('dsp/drums/analog_bass_drum.h', '// 808 bass drum model, revisited.', '// Analog bass drum model, revisited.'),
    neutral('dsp/drums/analog_snare_drum.h', '// 808 snare drum model, revisited.',
            '// Analog snare drum model, revisited.'),
    neutral('dsp/drums/analog_snare_drum.h', '// 808-style (2 modes)', '// The classic analog circuit (2 modes)'),
    neutral('dsp/drums/analog_snare_drum.h', '// What the 808 could have been if there were extra modes!',
            '// What the classic circuit could have been if there were extra modes!'),
    neutral('dsp/drums/hi_hat.h', '// 808 HH,', '// Analog HH,'),
    neutral('dsp/drums/hi_hat.h', '// 808 style "metallic noise"', '// Analog-style "metallic noise"'),
    neutral('dsp/drums/hi_hat.h', 'part of the 808 circuit!', 'part of the classic circuit!'),
    neutral('dsp/drums/synthetic_bass_drum.h', '// Inadvertently 909-ish.',
            '// Inadvertently close to a classic hybrid drum machine.'),
    neutral('dsp/drums/synthetic_snare_drum.h', 'taken from the 909 schematics:',
            'taken from a classic hybrid drum machine\'s schematics:'),
    neutral('dsp/drums/synthetic_snare_drum.h', '// The 909 circuit has', '// The original circuit has'),
    neutral('dsp/engine/bass_drum_engine.h', '// 808 and synthetic', '// Analog and synthetic'),
    neutral('dsp/engine/bass_drum_engine.cc', '// 808 and synthetic', '// Analog and synthetic'),
    neutral('dsp/engine/snare_drum_engine.h', '// 808 and synthetic', '// Analog and synthetic'),
    neutral('dsp/engine/snare_drum_engine.cc', '// 808 and synthetic', '// Analog and synthetic'),
    neutral('dsp/engine/hi_hat_engine.h', '// 808-style HH', '// Analog-style HH'),
    neutral('dsp/engine/hi_hat_engine.cc', '// 808-style HH', '// Analog-style HH'),
    neutral('dsp/engine/engine.h', 'modal drum, 808 kick)', 'modal drum, analog kick)'),
    neutral('dsp/engine/fm_engine.h', 'as found in Braids, Rings and Elements.', 'as found in the author\'s earlier modules.'),
    neutral('dsp/engine/fm_engine.cc', 'found in Braids, Rings and Elements.', 'found in the author\'s earlier modules.'),
    neutral('dsp/engine/speech_engine.cc', '// Interpolates between the 3 models: naive, SAM, LPC.',
            '// Interpolates between the 3 models: naive, phoneme, LPC.'),
    neutral('dsp/fm/algorithms.h', '// From DX7', '// The classic six-operator algorithms'),
    neutral('dsp/fm/dx_units.h', 'functions for DX7 patch data.', 'functions for six-operator patch data.'),
    neutral('dsp/fm/dx_units.cc', 'routines for DX7 patch data.', 'routines for six-operator patch data.'),
    neutral('dsp/fm/lfo.h', '// DX7-compatible LFO.', '// Six-operator-compatible LFO.'),
    neutral('dsp/fm/patch.h', '// DX7 patch.', '// Six-operator patch.'),
    neutral('dsp/fm/voice.h', '// DX7 voice.', '// Six-operator voice.'),
    neutral('dsp/fm/envelope.h', 'used in Plaits\' "envelope scrubbing" feature.',
            'used in the module\'s "envelope scrubbing" feature.'),
    neutral('dsp/fm/envelope.h', '// TODO(pichenettes):', '// TODO:'),
    neutral('dsp/physical_modelling/resonator.h', 'taken from Rings\' code', 'taken from the author\'s resonator module'),
    neutral('dsp/physical_modelling/resonator.cc', 'taken from Rings\' code', 'taken from the author\'s resonator module'),
    neutral('dsp/physical_modelling/string.h', 'the implementation used in Rings.',
            'the implementation used in the author\'s resonator module.'),
    neutral('dsp/physical_modelling/string.cc', 'the implementation used in Rings.',
            'the implementation used in the author\'s resonator module.'),
    neutral('dsp/physical_modelling/string_voice.h', 'the niceties from Rings.',
            'the niceties from the author\'s resonator module.'),
    neutral('dsp/physical_modelling/string_voice.cc', 'the niceties from Rings.',
            'the niceties from the author\'s resonator module.'),
    neutral('dsp/speech/sam_speech_synth.h', '// SAM-inspired speech synth (as used in Shruthi/Ambika/Braids).',
            '// Phoneme speech synth, after an early software speech synthesizer (as in the author\'s earlier\n'
            '// instruments).'),
    neutral('dsp/speech/sam_speech_synth.cc', '// SAM-inspired speech synth (as used in Shruthi/Ambika/Braids).',
            '// Phoneme speech synth, after an early software speech synthesizer (as in the author\'s earlier\n'
            '// instruments).'),
]

GENERIC = DSPUTIL_GENERIC + [  # Apply replacements to every file after patches.
    ('#include "plaits/', '#include "engines/macro/models/'),
    ('namespace plaits {', 'namespace usina_models {'),
    ('// namespace plaits', '// namespace usina_models'),
    ('plaits::', 'usina_models::'),
    ('PLAITS_', 'USINA_MODELS_'),
    ('dx_units', 'fm_units'), ('DX_UNITS', 'FM_UNITS'),
    ('vactrol', 'opto'),
    ('SAMSpeechSynth', 'PhonemeSpeechSynth'), ('kSAMNum', 'kPhonemeSynthNum'),
    ('sam_speech_synth', 'phoneme_speech_synth'), ('SAM_SPEECH_SYNTH', 'PHONEME_SPEECH_SYNTH'),
]

RENAMED = {'dsp/fm/dx_units.h': 'dsp/fm/fm_units.h', 'dsp/fm/dx_units.cc': 'dsp/fm/fm_units.cc',
           'dsp/speech/sam_speech_synth.h': 'dsp/speech/phoneme_speech_synth.h',
           'dsp/speech/sam_speech_synth.cc': 'dsp/speech/phoneme_speech_synth.cc'}


def split_banks(text):
    """resources.cc without the banks, and the banks alone (the personal build's file)."""
    start = text.index('const uint8_t syx_bank_0[] = {')
    end = text.index('};', text.index('const uint8_t* const fm_patches_table[] = {')) + 3
    banks = text[start:end]
    rest = text[:start] + text[end:]
    head = text[:text.index('#include')]
    banks_cc = (head + '// (USINA: the six-operator models\' banks alone, split out of resources.cc by the port script. Their\n'
                '// rights are not the author\'s to grant: compiled only by a personal build, CMake USINA_PERSONAL_FM_BANKS)\n\n'
                '#include "plaits/resources_fm_banks.h"\n\nnamespace plaits {\n\n' + banks + '\n}  // namespace plaits\n')
    banks_h = (head + '#ifndef PLAITS_RESOURCES_FM_BANKS_H_\n#define PLAITS_RESOURCES_FM_BANKS_H_\n\n'
               '#include "stmlib/stmlib.h"\n\nnamespace plaits {\n\n'
               '// (USINA: a personal build only - see resources_fm_banks.cc)\n'
               'extern const uint8_t syx_bank_0[];\nextern const uint8_t syx_bank_1[];\nextern const uint8_t syx_bank_2[];\n'
               'extern const uint8_t* const fm_patches_table[];\n\n'
               '}  // namespace plaits\n\n#endif  // PLAITS_RESOURCES_FM_BANKS_H_\n')
    return rest, banks_cc, banks_h


def main():
    src = os.path.join(upstream_dir('python tools/port/port_models.py UPSTREAM_DIR'), UPSTREAM[0])
    rev = git_head(src)
    base = os.path.join(src, UPSTREAM[1])
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
        if rel == 'resources.cc':
            text, banks_cc, banks_h = split_banks(text)
            outputs['resources_fm_banks.cc'] = banks_cc
            outputs['resources_fm_banks.h'] = banks_h
        outputs[RENAMED.get(rel, rel)] = apply_patches(KEY + rel, text, PATCHES, used)
    check_all_used(used)
    for rel, text in outputs.items():
        for old, new in GENERIC:
            text = text.replace(old, new)
        check_names(rel, text)
        write(OUT, rel, 'port_models.py', text)
    print('%d files -> %s (upstream %s)' % (len(outputs), OUT, rev))


if __name__ == '__main__':
    main()
