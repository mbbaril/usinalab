# Third-party code in RuidoCore

RuidoCore itself is licensed under the GNU Affero General Public License
v3 (see `LICENSE`). Parts of it are ports of code published by other
authors, whose own terms continue to apply to those parts. This file
records what came from where.

## Mutable Instruments (Émilie Gillet) — MIT

Most of the DSP under `include/ruidocore/Dsp/` is ported from the
Mutable Instruments Eurorack firmware
(<https://github.com/pichenettes/eurorack>), which is **MIT licensed**.
Nineteen of the twenty-two files there name the specific upstream source
they were ported from, in a comment at the top of the file.

MIT is compatible with the AGPL and the port may be distributed as part
of this AGPL work. MIT does, however, require that its copyright notice
and permission notice be retained. **They are currently not present in
those files** — only a prose credit naming the upstream file. That is an
attribution gap to close, not a licence conflict: the fix is to add the
upstream MIT header to each ported file.

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
