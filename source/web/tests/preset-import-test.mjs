// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Mario Brandalise Baril
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { readFileSync, existsSync, writeFileSync } from 'node:fs';
import { parseVstSound, applyVstSound } from '../preset-import.js';
import createUsina from '../../build-web/usina.mjs';
import { createHash } from 'node:crypto';
const require = createRequire(new URL('../../mobile/package.json', import.meta.url));
const { DOMParser } = require('@xmldom/xmldom');
class StrictParser extends DOMParser { constructor() { super({ onError: (_level, message) => { throw Error(message); } }); } }
const module = await createUsina();
const metadata = JSON.parse(module.UTF8ToString(module._web_schema()));
const text = readFileSync(new URL('./fixtures/native-sound.usina', import.meta.url), 'utf8');
const plan = parseVstSound(text, metadata, StrictParser);
assert.deepEqual(plan.blockers, []);
assert.equal(plan.params.cutoff, 33);
assert.equal(plan.params.dly_time, 777);
assert.equal(plan.routes.length, 1);
assert.equal(plan.routes[0].amount, .25);
assert.equal(plan.functions.length, 3);
assert(!('level' in plan.params));
const original = { params: { level: -18, feedback: 1.2, seq_bpm: 141, in_mix: .2, cutoff: 700, seq_play: 1 }, sequence: ['unchanged'], lanes: ['unchanged'], ccMaps: ['unchanged'], controllerBindings: ['unchanged'] };
const snapshot = JSON.stringify(original);
const applied = applyVstSound(original, plan, metadata);
assert.equal(JSON.stringify(original), snapshot);
assert.equal(applied.params.level, -18);
assert.equal(applied.params.seq_bpm, 141);
assert.equal(applied.params.in_mix, .2);
assert.equal(applied.params.cutoff, 33);
assert.equal(applied.params.feedback, 0);
assert.equal(applied.params.seq_play, 0);
assert.strictEqual(applied.sequence, original.sequence);
assert.strictEqual(applied.ccMaps, original.ccMaps);
const withoutDelay = parseVstSound(text.replace(/\s*<PARAM id="dly_time" value="[^"]*"\s*\/>/, ''), metadata, StrictParser);
assert.equal(applyVstSound({ ...original, params: { ...original.params, dly_time: 100 } }, withoutDelay, metadata).params.dly_time, 375);
assert.throws(() => parseVstSound('<OTHER/>', metadata, StrictParser));
assert.throws(() => parseVstSound(text.replace('stateVersion="6"', 'stateVersion="99"'), metadata, StrictParser));
assert.throws(() => parseVstSound('<!DOCTYPE USINA><USINA/>', metadata, StrictParser));
assert.throws(() => parseVstSound('<?xml-stylesheet href="https://invalid.test"?><USINA/>', metadata, StrictParser));
assert.throws(() => parseVstSound(text.replace('</USINA>', '<PARAM id="cutoff" value="100"/></USINA>'), metadata, StrictParser));
assert.throws(() => parseVstSound(text.replace('id="cutoff" value="33.0"', 'id="cutoff" value="NaN"'), metadata, StrictParser));
assert.throws(() => parseVstSound(text.slice(0, -12), metadata, StrictParser));
for (const changed of [text.replace('id="cutoff" value="33.0"', 'id="cutoff" value="999999"'), text.replace('</USINA>', '<PARAM id="unknown_engine" value="1"/></USINA>'), text.replace('id="fx_a_type" value="1.0"', 'id="fx_a_type" value="4.0"'), text.replace('src="lfo1"', 'src="unknown"')]) {
  const unsupported = parseVstSound(changed, metadata, StrictParser);
  assert(unsupported.blockers.length > 0);
  assert.throws(() => applyVstSound(original, unsupported, metadata));
  assert.equal(JSON.stringify(original), snapshot);
}
console.log(`Native JUCE fixture: ${Object.keys(plan.params).length} sound parameters, ${plan.routes.length} route; transactional import, rig preservation, feedback disarmed and malformed/unsupported input rejection: pass`);
const referenceUrl = new URL('../../build-web/parity/native-vst-preset.f32', import.meta.url);
if (existsSync(referenceUrl)) {
  const bytes = readFileSync(referenceUrl), reference = new Float32Array(bytes.buffer, bytes.byteOffset, bytes.length / 4);
  assert.equal(reference.length, 6144 * 2);
  assert.equal(module._web_init(48000, 4), 1);
  const index = new Map(metadata.parameters.map((p, i) => [p.id, i]));
  for (const [id, value] of Object.entries(plan.params)) module._web_set(index.get(id), value);
  module._web_commit();
  plan.routes.forEach((r, i) => module._web_route(i, r.source, r.destination, r.amount));
  const points = new Float32Array(module.HEAPF32.buffer, module._web_function_buffer(), 64 * 3);
  plan.functions.forEach((f, i) => { f.forEach((p, j) => points.set(p, j * 3)); module._web_function(i, f.length); });
  let energy = 0, error = 0, maxError = 0;
  for (let frame = 0; frame < 6144; frame += 128) {
    if (frame === 0) module._web_midi(144, 60, 100);
    if (frame === 4096) module._web_midi(128, 60, 0);
    module._web_render(128);
    for (let c = 0; c < 2; ++c) for (let i = 0, p = module._web_output(c) / 4; i < 128; ++i) {
      const expected = reference[(frame + i) * 2 + c], actual = module.HEAPF32[p + i], d = actual - expected;
      assert(Number.isFinite(actual)); energy += expected * expected; error += d * d; maxError = Math.max(maxError, Math.abs(d));
    }
  }
  const relative = Math.sqrt(error / Math.max(energy, 1e-20));
  const report = { source: 'actual JUCE PluginProcessor exported preset and audio vs imported sound in WASM adapter', sampleRate: 48000, oversampling: 4, frames: 6144, relative, maxError, passed: relative < 1e-4 || maxError < 1e-6, importerSha256: createHash('sha256').update(readFileSync(new URL('../preset-import.js', import.meta.url))).digest('hex') };
  report.wasmSha256=createHash('sha256').update(readFileSync(new URL('../../build-web/usina.wasm',import.meta.url))).digest('hex');
  report.moduleSha256=createHash('sha256').update(readFileSync(new URL('../../build-web/usina.mjs',import.meta.url))).digest('hex');
  writeFileSync(new URL('../../build-web/parity/native-vst-import-report.json', import.meta.url), JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report));
  assert(report.passed, 'Imported sound differs from the actual JUCE processor');
}
