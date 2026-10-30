// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import create from '../../build-web/usina.mjs';
const m=await create(),meta=JSON.parse(m.UTF8ToString(m._web_schema())),ids=new Map(meta.parameters.map((p,i)=>[p.id,i])),checks=[];
for(const fs of [44100,48000]){
  assert(m._web_init(fs,4));const set=(id,v)=>m._web_set(ids.get(id),v);set('fx_on',0);set('op1_rel',0);set('seq_on',1);set('seq_bpm',120);set('seq_play',1);m._web_commit();m._web_lane(0,16,0,1,1,0);m._web_step(0,0,1,48,100,1,1,.5);
  const notes=new Int32Array(m.HEAPF32.buffer,m._web_chord_buffer(),16),durations=new Float32Array(m.HEAPF32.buffer,m._web_duration_buffer(),16);notes.set([48,52]);durations.set([.5,2.5]);m._web_chord_durations(0,0,2);m._web_flush_edits();
  let frame=0;const until=t=>{const stop=Math.round(t*fs);while(frame<stop){const n=Math.min(128,stop-frame);m._web_render(n);frame+=n;}};
  until(.03);assert.equal(m._web_meter(0),2);until(.12);assert.equal(m._web_meter(0),1,'Short note must release while the other crosses steps');until(.36);assert.equal(m._web_meter(0),0,'Independent long tail must release');
  set('seq_play',0);m._web_commit();m._web_render(128);durations.set([64,64]);m._web_chord_durations(0,0,2);m._web_flush_edits();set('seq_play',1);m._web_commit();for(let i=0;i<16;i++)m._web_render(128);assert.equal(m._web_meter(0),2);set('seq_play',0);m._web_commit();for(let i=0;i<100;i++)m._web_render(128);assert.equal(m._web_meter(0),0,'Stop closes long tails');checks.push({sampleRate:fs,independentRelease:true,stopLongTails:true});m._web_dispose();
}
const hash=f=>createHash('sha256').update(readFileSync(new URL(f,import.meta.url))).digest('hex'),report={passed:true,wasmSha256:hash('../../build-web/usina.wasm'),moduleSha256:hash('../../build-web/usina.mjs'),checks};writeFileSync(new URL('../../build-web/parity/note-duration-audio-report.json',import.meta.url),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
