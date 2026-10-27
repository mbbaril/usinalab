// SPDX-License-Identifier: GPL-3.0-only
// Pure MIDI 1 MPE lower-zone encoder. No DOM, audio engine or transport dependencies.
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, Number.isFinite(v) ? v : lo));
const seven = v => Math.round(clamp(v, 0, 1) * 127);
export function pitchCoordinate(x, centres) {
  if (!Number.isFinite(x) || centres.length < 2) return 0;
  let i = 0; while (i < centres.length - 2 && x > centres[i + 1].x) ++i;
  const a = centres[i], b = centres[i + 1];
  return a.note + (x - a.x) / Math.max(.001, b.x - a.x) * (b.note - a.note);
}
export class MpeEncoder {
  constructor(emit, { range = 48, members = 15, configureChannel = false } = {}) {
    this.emit = emit; this.configureChannel=configureChannel;this.range = Math.round(clamp(range, 1, 96)); this.members = Math.round(clamp(members, 1, 15)); this.voices = new Map();
  }
  setup() {
    this.clear(); const packets = [[0xb0,101,0],[0xb0,100,6],[0xb0,6,this.members],[0xb0,101,127],[0xb0,100,127]];
    for (let channel = 0; channel <= this.members; ++channel) {
      const s = 0xb0 + channel;
      packets.push([s,101,0],[s,100,0],[s,6,channel ? this.range : 2],[s,38,0],[s,101,127],[s,100,127]);
    }
    return this.emit(packets);
  }
  begin(id, note, velocity = 100, pressure = 0, timbre = .5) {
    if (this.voices.has(id)) this.end(id);
    let channel = Array.from({length:this.members}, (_,i) => i + 1).find(c => ![...this.voices.values()].some(v => v.channel === c));
    if (channel === undefined) { const oldest = this.voices.keys().next().value; channel = this.voices.get(oldest).channel; this.end(oldest); }
    const voice = {channel, note:Math.round(clamp(note,0,127)), pitch:8192, pressure:seven(pressure), timbre:seven(timbre)};
    this.voices.set(id, voice);
    const c=0xb0+channel;
    const setup=this.configureChannel?[[c,101,0],[c,100,0],[c,6,this.range],[c,38,0],[c,101,127],[c,100,127]]:[];
    this.emit([...setup,[0xe0+channel,0,64],[c,74,voice.timbre],[0xd0+channel,voice.pressure,0],[0x90+channel,voice.note,Math.round(clamp(velocity,1,127))]]);
    return channel + 1;
  }
  expression(id, values) {
    const v = this.voices.get(id); if (!v) return;
    const packets = [];
    if (values.semitones !== undefined) {
      const normalized = clamp(values.semitones / this.range, -1, 1), pitch = 8192 + Math.round(normalized * (normalized >= 0 ? 8191 : 8192));
      if (pitch !== v.pitch) { v.pitch = pitch; packets.push([0xe0+v.channel,pitch&127,pitch>>7]); }
    }
    for (const key of ['pressure','timbre']) if (values[key] !== undefined) {
      const value = seven(values[key]); if (value === v[key]) continue; v[key] = value;
      packets.push(key === 'pressure' ? [0xd0+v.channel,value,0] : [0xb0+v.channel,74,value]);
    }
    if (packets.length) this.emit(packets);
  }
  end(id) {
    const v = this.voices.get(id); if (!v) return; this.voices.delete(id);
    this.emit([[0x80+v.channel,v.note,0],[0xd0+v.channel,0,0],[0xe0+v.channel,0,64],[0xb0+v.channel,74,64]]);
  }
  clear() { this.voices.clear(); }
}
