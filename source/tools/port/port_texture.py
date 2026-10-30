# SPDX-License-Identifier: GPL-3.0-only
"""The texture processor's port (MIT; THIRD_PARTY.md): the 2021 granular/delay module's code, with the DSP
utilities from the shared port (port_dsputil.py), copied from the read-only upstream clones and changed only by the
patches listed here, each with its reason. The output (src/fx/texture) is generated: never edit it by hand, edit
this script and run it again.

    python tools/port/port_texture.py UPSTREAM_DIR      (the folder holding the upstream clones)

Every patch must match exactly as many times as it says, or the script stops: an upstream change never slips
through silently. What the port is checked against: tools/port/check_texture.sh (bit for bit against the upstream
code itself, compiled in WSL) and tests/TextureTest.cpp (the plugin's build against the upstream's output hashes).
"""
import os
import re
import sys

from portlib import (DSPUTIL_GENERIC, USINA, apply_patches, check_all_used, check_names, git_head, upstream_dir,
                     write)

OUT = os.path.join(USINA, 'src', 'fx', 'texture')
UPSTREAM = ('eurorack', 'beads')  # the upstream clone's folder and the module's folder in it
KEY = 'beads/'

# Include processor DSP, input gain and resource tables; exclude unused wavetable synthesis.
MODULE_FILES = [
    'dsp/frame.h', 'dsp/mu_law.h', 'dsp/mu_law.cc', 'dsp/delay_line.h', 'dsp/grain_envelope.h',
    'dsp/voice_stealing_envelope.h', 'dsp/grain.h', 'dsp/delay_grain.h', 'dsp/granular_player.h',
    'dsp/delay_granular_player.h', 'dsp/randomized_parameter.h', 'dsp/trigger_generator.h',
    'dsp/trigger_generator.cc', 'dsp/modulated_parameter.h', 'dsp/sine_oscillator.h', 'dsp/delay_clock.h',
    'dsp/delay_clock.cc', 'dsp/delay_controller.h', 'dsp/delay_controller.cc', 'dsp/tap.h', 'dsp/multitap_delay.h',
    'dsp/limiter.h', 'dsp/sample_rate_conversion_filters.h', 'dsp/granular_processor.h', 'dsp/fx/fx_engine.h',
    'dsp/fx/reverb.h', 'dsp/fx/wow_and_flutter.h', 'dsp/processor.h', 'dsp/processor.cc',
    'gain_control.h', 'gain_control.cc', 'resources.h', 'resources.cc',
]

# Patches: source file, old text, new text, count and rationale. Apply before renaming.
PATCHES = [
    # frame.h: the codec's largest block (drivers/codec.h), for the fixed-size buffers below
    ('beads/dsp/frame.h',
     'namespace beads {\n',
     'namespace beads {\n\n// (USINA: the codec\'s largest block, from upstream\'s drivers/codec.h: the buffers that upstream sizes per\n'
     '// call - variable-length arrays, a GCC extension - are sized by it)\nconst size_t kMaxCodecBlockSize = 24;\n', 1,
     'the block-size constant the fixed-size buffers need'),
    ('beads/dsp/delay_line.h', '#include "stmlib/utils/dsp.h"\n', '', 1,
     'an include of which nothing is used (its integer interpolators, GCC-only attributes): left out'),
    # 12-bit items: GCC packs two 12-bit bit-fields in 3 bytes; MSVC cannot - the same values in 3 bytes, by hand
    ('beads/dsp/delay_line.h',
     'template<>\nstruct Item<RESOLUTION_12_BIT, 2> {\n  void Store(int i, FloatFrame s) {\n'
     '    l = stmlib::Clip16(static_cast<int32_t>(s.l * 32768.0f)) >> 4;\n'
     '    r = stmlib::Clip16(static_cast<int32_t>(s.r * 32768.0f)) >> 4;\n  }\n'
     '  inline FloatFrame value(int i) const {\n    FloatFrame result;\n'
     '    result.l = static_cast<float>(l) / 2048.0f;\n    result.r = static_cast<float>(r) / 2048.0f;\n'
     '    return result;\n  }\n  enum { bundle_size = 1 };\n  int16_t l : 12;\n  int16_t r : 12;\n'
     '} __attribute__((packed));\n',
     'template<>\nstruct Item<RESOLUTION_12_BIT, 2> {\n  void Store(int i, FloatFrame s) {\n'
     '    set12(bytes, 0, stmlib::Clip16(static_cast<int32_t>(s.l * 32768.0f)) >> 4);\n'
     '    set12(bytes, 1, stmlib::Clip16(static_cast<int32_t>(s.r * 32768.0f)) >> 4);\n  }\n'
     '  inline FloatFrame value(int i) const {\n    FloatFrame result;\n'
     '    result.l = static_cast<float>(get12(bytes, 0)) / 2048.0f;\n'
     '    result.r = static_cast<float>(get12(bytes, 1)) / 2048.0f;\n'
     '    return result;\n  }\n  enum { bundle_size = 1 };\n  uint8_t bytes[3];  // (USINA: l, r)\n};\n', 1,
     '12-bit stereo items: two signed 12-bit values in 3 bytes by hand (GCC\'s packed bit-fields; MSVC cannot '
     'pack bit-fields across bytes, and 4 bytes would overflow the memory banks). The same values'),
    ('beads/dsp/delay_line.h',
     'template<>\nstruct Item<RESOLUTION_12_BIT, 1> {\n  void Store(int i, FloatFrame s) {\n    int16_t lr = \\\n'
     '        stmlib::Clip16(static_cast<int32_t>((s.l + s.r) * 16384.0f)) >> 4;\n    if (i) {\n      odd = lr;\n'
     '    } else {\n      even = lr;\n    }\n  }\n  inline FloatFrame value(int i) const {\n    FloatFrame result;\n'
     '    result.l = result.r = static_cast<float>(i ? odd : even) / 2048.0f;\n    return result;\n  }\n'
     '  enum { bundle_size = 2 };\n  int16_t odd : 12;\n  int16_t even : 12;\n} __attribute__((packed));\n',
     'template<>\nstruct Item<RESOLUTION_12_BIT, 1> {\n  void Store(int i, FloatFrame s) {\n    int16_t lr = \\\n'
     '        stmlib::Clip16(static_cast<int32_t>((s.l + s.r) * 16384.0f)) >> 4;\n    if (i) {\n'
     '      set12(bytes, 0, lr);\n    } else {\n      set12(bytes, 1, lr);\n    }\n  }\n'
     '  inline FloatFrame value(int i) const {\n    FloatFrame result;\n'
     '    result.l = result.r = static_cast<float>(get12(bytes, i ? 0 : 1)) / 2048.0f;\n    return result;\n  }\n'
     '  enum { bundle_size = 2 };\n  uint8_t bytes[3];  // (USINA: odd, even)\n};\n', 1, '12-bit mono items: the same'),
    ('beads/dsp/delay_line.h',
     'template<Resolution resolution, int num_channels>\nstruct Item { };\n',
     'template<Resolution resolution, int num_channels>\nstruct Item { };\n\n'
     '// (USINA: two signed 12-bit fields in 3 bytes, field 0 in the low bits - as GCC lays out the packed bit-fields)\n'
     'inline void set12(uint8_t* b, int field, int32_t v) {\n'
     '  uint32_t u = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16);\n'
     '  const int shift = field ? 12 : 0;\n'
     '  u = (u & ~(0xfffu << shift)) | ((uint32_t(v) & 0xfffu) << shift);\n'
     '  b[0] = uint8_t(u);\n  b[1] = uint8_t(u >> 8);\n  b[2] = uint8_t(u >> 16);\n}\n\n'
     'inline int32_t get12(const uint8_t* b, int field) {\n'
     '  const uint32_t u = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16);\n'
     '  const int32_t v = int32_t((u >> (field ? 12 : 0)) & 0xfffu);\n  return v >= 2048 ? v - 4096 : v;\n}\n', 1,
     '(the helpers)'),
    # variable-length arrays (a GCC extension): fixed at the codec's largest block
    ('beads/dsp/processor.cc',
     '  TriggerGenerator::Output triggers_buffer[size];\n  DelayController::Output taps_buffer[size];\n'
     '  bool gate_out[size];\n',
     '  assert(size <= kMaxCodecBlockSize);  // (USINA: fixed buffers, see frame.h)\n'
     '  TriggerGenerator::Output triggers_buffer[kMaxCodecBlockSize];\n'
     '  DelayController::Output taps_buffer[kMaxCodecBlockSize];\n  bool gate_out[kMaxCodecBlockSize];\n', 1,
     'variable-length arrays (GCC) at the largest block'),
    ('beads/dsp/processor.cc', '  FloatFrame dry_signal[size];\n', '  FloatFrame dry_signal[kMaxCodecBlockSize];\n', 1,
     '(the same)'),
    ('beads/dsp/processor.cc', '#include <algorithm>\n', '#include <algorithm>\n#include <cassert>\n', 1,
     '(assert)'),
    ('beads/dsp/granular_processor.h',
     '      bool d_freeze[d_size];\n      TriggerGenerator::Output d_triggers[d_size];\n'
     '      DelayController::Output d_taps[d_size];\n      FloatFrame d_in[d_size];\n      FloatFrame d_out[d_size];\n'
     '      float downsampled[d_size];\n      float upsampled[size];\n',
     '      bool d_freeze[kMaxCodecBlockSize];\n      TriggerGenerator::Output d_triggers[kMaxCodecBlockSize];\n'
     '      DelayController::Output d_taps[kMaxCodecBlockSize];\n      FloatFrame d_in[kMaxCodecBlockSize];\n'
     '      FloatFrame d_out[kMaxCodecBlockSize];\n      float downsampled[kMaxCodecBlockSize];\n'
     '      float upsampled[kMaxCodecBlockSize];\n', 1, '(the same)'),
    ('beads/dsp/trigger_generator.cc',
     '    stmlib::GateFlags merged_gate_flags[size];\n',
     '    stmlib::GateFlags merged_gate_flags[kMaxCodecBlockSize];  // (USINA: fixed, see frame.h)\n', 1,
     '(the same)'),
    ('beads/dsp/trigger_generator.cc', '#include "beads/dsp/trigger_generator.h"\n',
     '#include "beads/dsp/trigger_generator.h"\n#include "beads/dsp/frame.h"\n', 1, '(kMaxCodecBlockSize)'),
    # the input gain control reads floats, not the codec's 32-bit integers
    ('beads/gain_control.h', '#include "beads/drivers/codec.h"\n', '', 1,
     'the codec driver: the plugin\'s input is float'),
    ('beads/gain_control.h', '      const Codec::Frame* in,\n', '      const FloatFrame* in,\n', 1, '(the same)'),
    ('beads/gain_control.cc', '    const Codec::Frame* in,\n', '    const FloatFrame* in,\n', 1, '(the same)'),
    ('beads/gain_control.cc',
     re.compile(r'    int32_t l_int32 = left_normalization \? in->r : in->l;\n'
                r'    int32_t r_int32 = right_normalization \? in->l : in->r;\n[ \t]*\n'
                r'    float l = static_cast<float>\(l_int32\) / 2147483648\.0f;\n'
                r'    float r = static_cast<float>\(r_int32\) / 2147483648\.0f;\n'),
     '    // (USINA: the input as floats; upstream converts the codec\'s 32-bit integers here, x / 2^31)\n'
     '    float l = left_normalization ? in->r : in->l;\n    float r = right_normalization ? in->l : in->r;\n', 1,
     '(the same: for an input that is a codec integer / 2^31, the same value)'),
    # the tables: only the sine (the wavetables are the synth mode's)
    ('beads/resources.h',
     re.compile(r'extern const int16_t\* lookup_table_i16_table\[\];\n\nextern const int8_t\* lookup_table_i8_table\[\];\n\n'
                r'extern const int16_t\* wavetables_table\[\];\n\n'),
     '', 1, 'the tables of the synth mode, left out'),
    ('beads/resources.h',
     re.compile(r'extern const int16_t wav_integrated_waves\[\];\n'), '', 1, '(the same)'),
    ('beads/resources.h',
     re.compile(r'#define WAV_INTEGRATED_WAVES 0\n#define WAV_INTEGRATED_WAVES_SIZE 49920\n'), '', 1, '(the same)'),
]

GENERIC = DSPUTIL_GENERIC + [  # Apply replacements to every file after patches.
    ('#include "beads/dsp/', '#include "fx/texture/dsp/'),
    ('#include "beads/resources.h"', '#include "fx/texture/resources.h"'),
    ('#include "beads/gain_control.h"', '#include "fx/texture/gain_control.h"'),
    ('namespace beads {', 'namespace usina_texture {'),
    ('// namespace beads', '// namespace usina_texture'),
    ('BEADS_', 'USINA_TEXTURE_'),
]


def resources_cc(text):
    """Only the sine table (and the table list naming it): the rest is the synth mode's wavetables."""
    start = text.index('const float* lookup_table_table[] = {')
    end = text.index('};', start) + 3
    head = text[:end]
    return head + '\n}  // namespace beads\n'


def pot_channel_h(cv_reader):
    """The module's knob laws: class PotChannel from cv_reader.h (the rest of that file reads the hardware's ADCs),
    verbatim but for its constructor and destructor, which upstream declares and never defines."""
    start = cv_reader.index('class PotChannel {')
    end = cv_reader.index('\n};\n', start) + 4
    body = cv_reader[start:end]
    for old, new in (('  PotChannel();\n', '  PotChannel() { }  // (USINA: declared upstream, never defined)\n'),
                     ('  ~PotChannel();\n', '  ~PotChannel() { }\n')):
        if body.count(old) != 1:
            sys.exit('pot_channel: "%s" not found once' % old.strip())
        body = body.replace(old, new)
    head = cv_reader[:cv_reader.index('#ifndef BEADS_CV_READER_H_')]
    return (head + '#ifndef BEADS_POT_CHANNEL_H_\n#define BEADS_POT_CHANNEL_H_\n\n'
            '// (USINA: class PotChannel of upstream\'s cv_reader.h, alone)\n\n'
            '#include <algorithm>\n#include <cmath>\n\n#include "stmlib/stmlib.h"\n#include "stmlib/dsp/dsp.h"\n'
            '#include "stmlib/dsp/hysteresis_filter.h"\n\nnamespace beads {\n\n' + body +
            '\n}  // namespace beads\n\n#endif  // BEADS_POT_CHANNEL_H_\n')


def main():
    src = os.path.join(upstream_dir('python tools/port/port_texture.py UPSTREAM_DIR'), UPSTREAM[0])
    rev = git_head(src)
    base = os.path.join(src, UPSTREAM[1])
    sources = [(KEY + f, os.path.join(base, f)) for f in MODULE_FILES]
    sources += [(KEY + 'pot_channel.h', os.path.join(base, 'cv_reader.h'))]
    used = {i: 0 for i in range(len(PATCHES))}
    for key, path in sources:
        with open(path, encoding='utf-8', newline='') as fh:
            text = fh.read().replace('\r\n', '\n')
        if key == KEY + 'resources.cc':
            text = resources_cc(text)
        if key == KEY + 'pot_channel.h':
            text = pot_channel_h(text)
        text = apply_patches(key, text, PATCHES, used)
        for old, new in GENERIC:
            text = text.replace(old, new)
        rel = key[len(KEY):]
        check_names(rel, text)
        write(OUT, rel, 'port_texture.py', text)
    check_all_used(used)
    print('%d files -> %s (upstream %s)' % (len(sources), OUT, rev))


if __name__ == '__main__':
    main()
