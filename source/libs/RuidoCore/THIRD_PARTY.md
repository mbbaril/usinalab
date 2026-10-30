# Third-party code in RuidoCore

RuidoCore itself is licensed under the GNU Affero General Public License
v3 (see `LICENSE`). Parts of it are ports of code published by other
authors, whose own terms continue to apply to those parts. This file
records what came from where.

## Mutable Instruments (Émilie Gillet) — MIT

Most of the DSP under `include/ruidocore/Dsp/` and several engines under
`include/ruidocore/Synth/` are ported from the Mutable Instruments
Eurorack firmware (<https://github.com/pichenettes/eurorack>, the Plaits
module) and from stmlib (<https://github.com/pichenettes/stmlib>), both
**MIT licensed**. MIT is compatible with the AGPL and the ports are
distributed as part of this AGPL work. Each ported file keeps the
upstream copyright and permission notice in its header, which points
here for provenance. The upstream names stay out of the code: files,
classes and constants use descriptive names.

| RuidoCore file | Upstream source |
|---|---|
| `include/ruidocore/Dsp/AnalogBassDrum.h` | `plaits/dsp/drums/analog_bass_drum.h` |
| `include/ruidocore/Dsp/AnalogHiHat.h` | `plaits/dsp/drums/hi_hat.h` |
| `include/ruidocore/Dsp/AnalogSnareDrum.h` | `plaits/dsp/drums/analog_snare_drum.h` |
| `include/ruidocore/Dsp/ClockedNoise.h` | `plaits/dsp/noise/clocked_noise.h`, `stmlib/dsp/polyblep.h` |
| `include/ruidocore/Dsp/CombString.h` | `plaits/dsp/physical_modelling/string.h/.cc` |
| `include/ruidocore/Dsp/DcBlocker.h` | `stmlib/dsp/filter.h` |
| `include/ruidocore/Dsp/DecayEnvelope.h` | `plaits/dsp/envelope.h` |
| `include/ruidocore/Dsp/DelayLine.h` | `plaits/dsp/physical_modelling/delay_line.h` |
| `include/ruidocore/Dsp/Downsampler4x.h` | `plaits/dsp/downsampler/4x_downsampler.h`, `plaits/resources.cc` |
| `include/ruidocore/Dsp/FmQuantizerTable.h`, `src/FmQuantizerTable.cpp` | `plaits/resources.cc` |
| `include/ruidocore/Dsp/Limiter.h` | `stmlib/dsp/limiter.h` |
| `include/ruidocore/Dsp/LowPassGate.h` | `plaits/dsp/envelope.h` (`opto*`: upstream's `vactrol_*`) |
| `include/ruidocore/Dsp/Overdrive.h` | `plaits/dsp/fx/overdrive.h` |
| `include/ruidocore/Dsp/Particle.h` | `plaits/dsp/noise/particle.h` |
| `include/ruidocore/Dsp/Resonator.h` | `plaits/dsp/physical_modelling/resonator.h/.cc`, `plaits/resources.cc` |
| `include/ruidocore/Dsp/ResonatorTables.h`, `src/ResonatorTables.cpp` | `plaits/resources.h/.cc` |
| `include/ruidocore/Dsp/SimpleDiffuser.h` | `plaits/dsp/fx/diffuser.h` |
| `include/ruidocore/Dsp/SoftClip.h` | `stmlib/dsp/dsp.h` |
| `include/ruidocore/Dsp/SwarmVoice.h` | `plaits/dsp/engine/swarm_engine.h` |
| `include/ruidocore/Dsp/WaveBankData.h`, `src/WaveBankData.cpp` | `plaits/resources.h/.cc` (the integrated waves) |
| `include/ruidocore/Synth/BassDrumEngine.h` | `plaits/dsp/engine/bass_drum_engine.h/.cc`, `plaits/dsp/fx/overdrive.h` |
| `include/ruidocore/Synth/FilteredNoiseEngine.h` | `plaits/dsp/engine/noise_engine.h/.cc` |
| `include/ruidocore/Synth/FmEngine.h` | `plaits/dsp/engine/fm_engine.h/.cc`, `plaits/dsp/voice.cc` |
| `include/ruidocore/Synth/HiHatEngine.h` | `plaits/dsp/engine/hi_hat_engine.h/.cc` |
| `include/ruidocore/Synth/ModalEngine.h` | `plaits/dsp/engine/modal_engine.h/.cc`, `plaits/dsp/physical_modelling/modal_voice.h/.cc` |
| `include/ruidocore/Synth/ParticleNoiseEngine.h` | `plaits/dsp/engine/particle_engine.h/.cc` |
| `include/ruidocore/Synth/SnareDrumEngine.h` | `plaits/dsp/engine/snare_drum_engine.h/.cc` |
| `include/ruidocore/Synth/StringEngine.h` | `plaits/dsp/engine/string_engine.h/.cc`, `plaits/dsp/physical_modelling/string_voice.h/.cc` |
| `include/ruidocore/Synth/SwarmEngine.h` | `plaits/dsp/engine/swarm_engine.h/.cc` |
| `include/ruidocore/Synth/WavetableEngine.h` | `plaits/dsp/voice.cc` |

The upstream revision of each port was not recorded when it was read.

The upstream notice is:

    Copyright 2016 Emilie Gillet.

    Permission is hereby granted, free of charge, to any person obtaining
    a copy of this software and associated documentation files (the
    "Software"), to deal in the Software without restriction, including
    without limitation the rights to use, copy, modify, merge, publish,
    distribute, sublicense, and/or sell copies of the Software, and to
    permit persons to whom the Software is furnished to do so, subject to
    the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
    BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
    ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

### One Mutable module that is NOT MIT

`docs/SYNTH_ENGINE_RESEARCH.md` records that **Grids** is GPLv3 rather
than MIT, unlike Plaits/Tides2/Clouds/Stages/Warps/Marbles. Nothing from
Grids has been ported. Keep it that way unless the licence implication is
handled deliberately.

## JUCE

`ruidocore_modshapes` is the one module that links JUCE (`juce_core`,
for `juce::File`). JUCE is dual-licensed: JUCE 7 offers GPL or a
commercial licence, JUCE 8 offers **AGPLv3** or a commercial licence. The
open-source route is what this project uses, which is part of why this
project is AGPL.
