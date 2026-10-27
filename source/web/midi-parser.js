// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Mario Baril
// Stateful MIDI 1.0 byte parser: handles running status, real-time interleaving and fragmented messages.
// SysEx is deliberately ignored; an untrusted peripheral cannot grow an unbounded allocation.
export class MidiParser {
  constructor(emit) { this.emit = emit; this.reset(); }
  reset() { this.status = 0; this.need = 0; this.count = 0; this.first = 0; this.sysex = false; }
  push(bytes, timestamp = performance.now()) {
    for (const b of bytes) {
      if (b >= 0xf8) { this.emit({ status: b, a: 0, b: 0, timestamp }); continue; }
      if (b & 0x80) {
        if (b === 0xf0) { this.sysex = true; this.status = 0; continue; }
        if (b === 0xf7) { this.sysex = false; this.status = 0; continue; }
        this.sysex = false; this.status = b; this.count = 0;
        this.need = b < 0xf0 ? ([0xc0, 0xd0].includes(b & 0xf0) ? 1 : 2) : b === 0xf2 ? 2 : b === 0xf1 || b === 0xf3 ? 1 : 0;
        if (!this.need) { this.emit({ status: b, a: 0, b: 0, timestamp }); this.status = 0; }
      } else if (!this.sysex && this.status && this.need) {
        if (this.count++ === 0) this.first = b;
        if (this.count === this.need) {
          this.emit({ status: this.status, a: this.first, b: this.need === 2 ? b : 0, timestamp });
          this.count = 0; if (this.status >= 0xf0) this.status = 0;
        }
      }
    }
  }
}
