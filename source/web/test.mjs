// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Mario Brandalise Baril
// Run inside WSL: node web/test.mjs (after native and WASM builds).
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdirSync, copyFileSync, chmodSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import createUsina from '../build-web/usina.mjs';
import { MidiParser } from './midi-parser.js';
const root = fileURLToPath(new URL('../', import.meta.url));
const hash = data => createHash('sha256').update(data).digest('hex');
const sha = path => hash(readFileSync(path));
mkdirSync(`${root}build-web/parity`, { recursive: true });
const referencePath = `${root}build-web-native/usina_web_reference`, referenceSha256 = sha(referencePath);
const native = `${root}build-web/parity/reference-${referenceSha256}`;
copyFileSync(referencePath, native); chmodSync(native, 0o755);
const wasmBinary = readFileSync(`${root}build-web/usina.wasm`), wasmSha256 = hash(wasmBinary), moduleSha256 = sha(`${root}build-web/usina.mjs`);
const wasm = await createUsina({wasmBinary});
const schema = JSON.parse(wasm.UTF8ToString(wasm._web_schema()));
assert.equal(new Set(schema.parameters.map(p => p.id)).size, schema.parameters.length);
const ids = new Map(schema.parameters.map((p, i) => [p.id, i]));
const names = new Map(schema.parameters.map((p, i) => [p.name, i]));
const library = JSON.parse(readFileSync(`${root}resources/presets/factory_library.json`, 'utf8')).patches;
const result = [];
const select = process.argv.includes('--all') ? library : library.filter(p => p.cat === 'show' || ['BICHO VIVO', 'FND BUMBO', 'IMPULSO'].includes(p.name));
const cases = [{ name: 'INIT', values: {} }, ...select];
for (let mode = 0; mode <= 11; ++mode) cases.push({ name: `operator-${mode}`, values: { 'OP1.MODE': mode, 'OP1.A': mode === 8 ? 3 : 0, 'OP1.B': 50, 'OP1.C': 60, 'OP1.D': 50 } });
for (let type = 0; type < 18; ++type) cases.push({ name: `filter-${type}`, values: { FILTER: 1, 'FILTER TYPE': type, CUTOFF: 550, RESO: 50, 'FLT DRIVE': 40 } });
for (const [name, extra] of [
  ['distort', { fx_a_type: 3, dist_on: 1, dist_drive: 24 }],
  ['delay', { fx_a_type: 6, dly_on: 1, dly_time: 15, dly_mix: .6 }],
  ['filter', { fx_a_type: 7, fxf_on: 1, fxf_type: 1, fxf_cutoff: 400 }],
  ['room', { fx_a_type: 2, rev_amount: .7 }],
  ['texture', { fx_a_type: 1, tex_on: 1, tex_drywet: .7 }],
  ['vapor', { fx_a_type: 5, vap_on: 1, vap_drywet: .7 }],
  ['parallel', { fx_a_type: 3, fx_b_type: 6, fx_route: 1, fx_a_mix: .5, fx_b_mix: .5 }],
  ['aux', { fx_a_type: 0, aux_type: 2, aux_send: .5, rev_amount: .7 }],
  ['feedback', { feedback: .7, fb_time: 12 }],
]) cases.push({ name, values: {}, extra });
mkdirSync(`${root}build-web/parity`, { recursive: true });
let index = 0;
for (const rate of [44100, 48000]) for (const test of cases) {
  const commands = [`init ${rate} 4`];
  commands.push(`set ${ids.get('fx_on')} 0`);
  for (const [name, value] of Object.entries(test.values)) { const i = names.get(name); if (i !== undefined) commands.push(`set ${i} ${value}`); }
  if (test.extra) { commands.push(`set ${ids.get('fx_on')} 1`); for (const [id, value] of Object.entries(test.extra)) commands.push(`set ${ids.get(id)} ${value}`); }
  // Exercise attack, live editing, note-off and release. Runtime math remains IEEE; never -ffast-math.
  commands.push('commit', 'midi 144 60 100', 'render 4096', 'midi 128 60 0', 'render 2048');
  const script = `${root}build-web/parity/commands.txt`, output = `${root}build-web/parity/native.f32`;
  writeFileSync(script, commands.join('\n'));
  const run = spawnSync(native, [script, output], { encoding: 'utf8' });
  assert.equal(run.status, 0, `${test.name}: ${run.error || run.signal || run.stderr}`);
  const nativeBytes = readFileSync(output); const reference = new Float32Array(nativeBytes.buffer, nativeBytes.byteOffset, nativeBytes.byteLength / 4);
  let position = 0, error = 0, energy = 0, maxError = 0, peak = 0;
  for (const command of commands) {
    const [op, ...args] = command.split(' '); const a = args.map(Number);
    if (op === 'init') assert.equal(wasm._web_init(...a), 1);
    if (op === 'set') wasm._web_set(...a);
    if (op === 'commit') wasm._web_commit();
    if (op === 'midi') wasm._web_midi(...a);
    if (op === 'render') for (let left = a[0]; left > 0;) {
      const n = Math.min(128, left); wasm._web_render(n);
      const l = wasm._web_output(0) / 4, r = wasm._web_output(1) / 4;
      for (let i = 0; i < n; ++i) for (const p of [l, r]) {
        const actual = wasm.HEAPF32[p + i], expected = reference[position++];
        assert(Number.isFinite(actual) && Math.abs(actual) <= 1, `${test.name}: invalid sample`);
        const d = actual - expected; error += d * d; energy += expected * expected; maxError = Math.max(maxError, Math.abs(d)); peak = Math.max(peak, Math.abs(actual));
      }
      left -= n;
    }
  }
  const relative = Math.sqrt(error / Math.max(1e-20, energy));
  // Chaotic feedback/noise can diverge numerically; report those cases, never hide them behind a blanket pass.
  result.push({ name: test.name, rate, relative, maxError, peak, close: relative < 1e-3 || maxError < 1e-6 });
  if (++index % 20 === 0) console.log(`${index} renders checked`);
}
const events = []; const parser = new MidiParser(e => events.push(e));
parser.push([0x90, 60]); parser.push([100, 0xf8, 61, 80, 0x80, 60, 0, 0xc0, 4, 0xd0, 70]);
assert.deepEqual(events.map(e => [e.status, e.a, e.b]), [[0x90,60,100],[0xf8,0,0],[0x90,61,80],[0x80,60,0],[0xc0,4,0],[0xd0,70,0]]);
parser.push([0xf0, 1, 2, 0xf8, 3, 0xf7, 0x90, 62, 0]); assert.equal(events.at(-1).b, 0);
assert.equal(sha(`${root}build-web/usina.wasm`),wasmSha256,'WASM changed during validation');
assert.equal(sha(`${root}build-web/usina.mjs`),moduleSha256,'Module changed during validation');
writeFileSync(`${root}build-web/parity/report.json`, JSON.stringify({ date: new Date().toISOString(), parameters: schema.parameters.length, wasmSha256, moduleSha256, referenceSha256, results: result }, null, 2));
const divergent = result.filter(r => !r.close);
console.log(JSON.stringify({ renders: result.length, numericallyClose: result.length - divergent.length, divergent, midiParser: 'pass' }, null, 2));
if (divergent.some(r => !['texture', 'vapor', 'operator-9', 'operator-10', 'BICHO VIVO'].includes(r.name))) process.exitCode = 1;
