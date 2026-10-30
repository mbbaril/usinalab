// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Mario Brandalise Baril

// Wire format owned by mobile/android/app/src/main/cpp/native_audio.cpp.
export const NativeOp = Object.freeze({set:1, reset:2, commit:3, midi:4, route:5, step:6,
  expression:7, chord:8, lane:9, base:10, globals:11, flush:12, function:13, control:14, panic:15, scheduledMidi:16});
const finite = v => typeof v === 'number' && Number.isFinite(v) && Number.isFinite(Math.fround(v));
const integer = (v, min, max) => Number.isInteger(v) && v >= min && v <= max;
const requireValue = (ok, name) => { if (!ok) throw Error(`Mensagem de áudio inválida: ${name}`); };

/** Converts a complete Worklet edit to one atomic native batch; never publishes a partial edit. */
export class NativeMessageEncoder {
  constructor({scheduledMidi = false} = {}) { this.mono = Array(9).fill(false); this.scheduledMidi = scheduledMidi; }
  encode(data) {
    const ops = [], mono = [...this.mono], op = NativeOp;
    const append = (code, ...args) => {
      requireValue(args.every(finite), data.type);
      ops.push(code, ...args);
    };
    const laneId = l => requireValue(integer(l, 0, 8), 'pista');
    const expression = (l, i, s = {}) => append(op.expression, l, i, s.accent ?? 0,
      s.late ?? 0, s.condition ?? 0, s.pitchOffset ?? 0, +!!s.inherit);
    const chord = (l, i, s = {}) => {
      const notes = Array.isArray(s.notes) ? s.notes : s.durations ? [s.note] : null;
      const list = mono[l] ? (s.durations?.[s.note] ? [s.note] : []) : notes || [];
      const selected = list.slice(0, 16);
      requireValue(selected.every(n => integer(n, 0, 127)), 'acorde');
      append(op.chord, l, i, selected.length, ...selected, ...selected.map(n => s.durations?.[n] ?? 0));
    };
    const step = (l, i, s) => {
      requireValue(s && typeof s === 'object', 'passo');
      append(op.step, l, i, +s.on, s.note, s.velocity, s.probability, s.ratchet, s.gate);
      expression(l, i, s); chord(l, i, s);
    };
    const part = (l, settings, steps) => {
      laneId(l);
      requireValue(settings && Array.isArray(steps) && steps.length <= 64, 'padrão');
      mono[l] = !!settings.mono;
      append(op.lane, l, settings.length, settings.direction, settings.mul, settings.div, +settings.mute);
      append(op.base, l, settings.baseNote ?? (l >= 5 ? [36, 41, 48, 55][l - 5] : 60));
      steps.forEach((s, i) => step(l, i, s));
    };
    switch (data.type) {
      case 'params': case 'patch':
        requireValue(Array.isArray(data.values), 'parâmetros');
        if (data.type === 'patch') append(op.reset);
        for (const pair of data.values) {
          requireValue(Array.isArray(pair) && pair.length === 2 && integer(pair[0], 0, 65535), 'parâmetro');
          append(op.set, ...pair);
        }
        append(op.commit); break;
      case 'midi':
        requireValue(integer(data.status, 0x80, 0xff) && integer(data.a, 0, 127) && integer(data.b, 0, 127), 'MIDI');
        if (data.frame !== undefined) {
          requireValue(this.scheduledMidi && integer(data.frame,0,2**48-1), 'MIDI agendado');
          append(op.scheduledMidi,data.frame % 2**24,Math.floor(data.frame/2**24),data.status,data.a,data.b);
        } else append(op.midi, data.status, data.a, data.b);
        break;
      case 'route': append(op.route, data.index, data.source, data.destination, data.amount); break;
      case 'step': {
        requireValue(Array.isArray(data.values) && data.values.length === 8, 'passo');
        const [l, i] = data.values; laneId(l); requireValue(integer(i, 0, 63), 'passo');
        append(op.step, ...data.values); expression(l, i, data.expression);
        chord(l, i, data.expression ?? {notes:data.notes}); append(op.flush); break;
      }
      case 'lane':
        requireValue(Array.isArray(data.values) && data.values.length === 6, 'pista');
        laneId(data.values[0]); append(op.lane, ...data.values); append(op.flush); break;
      case 'part': part(data.lane, data.settings, data.steps); append(op.flush); break;
      case 'sequence':
        requireValue(Array.isArray(data.sequence) && data.sequence.length <= 9 && Array.isArray(data.lanes), 'sequência');
        data.sequence.forEach((steps, l) => part(l, data.lanes[l], steps)); append(op.flush); break;
      case 'control': append(op.control, data.index, data.value); break;
      case 'function':
        requireValue(Array.isArray(data.points) && data.points.length >= 2 && data.points.length <= 64 &&
          data.points.every(p => Array.isArray(p) && p.length === 3), 'função');
        append(op.function, data.index, data.points.length, ...data.points.flat()); break;
      case 'maintenance': append(op.flush); break;
      case 'globals': append(op.globals, data.resolution, data.swing); break;
      case 'panic': append(op.panic); break;
      default: throw Error(`Mensagem de áudio não suportada: ${data.type}`);
    }
    requireValue(ops.length < 65535, 'lote excede o ring nativo');
    this.mono = mono;
    return ops;
  }
}
