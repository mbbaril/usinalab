// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import create from '../../build-web/usina.mjs';
const m=await create(),meta=JSON.parse(m.UTF8ToString(m._web_schema())),ids=new Map(meta.parameters.map((p,i)=>[p.id,i]));
const checks=[];
for(const fs of [44100,48000]){
  assert.equal(m._web_init(fs,4),1);const set=(id,v)=>m._web_set(ids.get(id),v);set('fx_on',0);set('op1_rel',0);set('seq_on',1);set('seq_bpm',20);set('seq_play',1);m._web_commit();
  m._web_lane(0,1,0,1,1,0);m._web_step(0,0,1,48,100,1,1,.95);const chord=new Int32Array(m.HEAPF32.buffer,m._web_chord_buffer(),16);chord.set([48,52,55]);m._web_chord(0,0,3);m._web_flush_edits();
  const samples=new Float32Array(8192);for(let i=0;i<16;i++)m._web_render(128);for(let at=0;at<samples.length;at+=128){m._web_render(128);samples.set(m.HEAPF32.subarray(m._web_output(0)/4,m._web_output(0)/4+128),at);}assert(samples.every(Number.isFinite));
  const amp=hz=>{let re=0,im=0;for(let i=0;i<samples.length;i++){const t=i*2*Math.PI*hz/fs;re+=samples[i]*Math.cos(t);im+=samples[i]*Math.sin(t);}return 2*Math.hypot(re,im)/samples.length;};const levels=[130.8128,164.8138,195.9977].map(amp);assert(levels.every(v=>v>.005),`Missing chord component at ${fs}: ${levels}`);
  set('seq_play',0);m._web_commit();for(let i=0;i<800;i++)m._web_render(128);assert.equal(m._web_meter(0),0,'Sequencer stop must release all chord voices');
  checks.push({sampleRate:fs,levels,passed:true});m._web_dispose();
}
const hash=file=>createHash('sha256').update(readFileSync(new URL(file,import.meta.url))).digest('hex');
const report={wasmSha256:hash('../../build-web/usina.wasm'),moduleSha256:hash('../../build-web/usina.mjs'),checks,passed:true};writeFileSync(new URL('../../build-web/parity/sequence-audio-report.json',import.meta.url),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
