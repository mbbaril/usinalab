// SPDX-License-Identifier: GPL-3.0-only
// Measure the complete WASM audio adapter, not just its MIDI encoder.
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import create from '../../build-web/usina.mjs';
const wasm=await create(),meta=JSON.parse(wasm.UTF8ToString(wasm._web_schema())),ids=new Map(meta.parameters.map((d,i)=>[d.id,i]));
const checks=[];
{
  const processor=readFileSync(new URL('../../src/plugin/PluginProcessor.cpp',import.meta.url),'utf8');
  const nativeIds=[...processor.matchAll(/ParameterID\s*\{\s*"([^"]+)"/g)].map(m=>m[1]).filter(id=>id==='bend_range'||id.startsWith('mpe_'));
  assert(nativeIds.includes('mpe_mode')&&nativeIds.includes('mpe_bend_range'));
  assert.deepEqual(meta.parameters.filter(p=>nativeIds.includes(p.id)).map(p=>p.id),nativeIds);
  for(const id of nativeIds)assert.equal(meta.parameters.filter(p=>p.id===id).length,1,`Unique MIDI parameter ${id}`);
}
for(const rate of [44100,48000]){
  assert.equal(wasm._web_init(rate,4),1);const set=(id,v)=>wasm._web_set(ids.get(id),v),midi=(...m)=>wasm._web_midi(...m);
  set('fx_on',0);set('op1_rel',0);wasm._web_commit();
  const render=()=>{const length=9600,samples=new Float32Array(length);for(let i=0;i<Math.ceil(rate*.25/128);i++)wasm._web_render(128);for(let at=0;at<length;){const n=Math.min(128,length-at);wasm._web_render(n);samples.set(wasm.HEAPF32.subarray(wasm._web_output(0)/4,wasm._web_output(0)/4+n),at);at+=n;}assert(samples.every(v=>Number.isFinite(v)&&Math.abs(v)<=1));return hz=>{let re=0,im=0;for(let i=0;i<length;i++){const p=2*Math.PI*hz*i/rate;re+=samples[i]*Math.cos(p);im+=samples[i]*Math.sin(p);}return 2*Math.hypot(re,im)/length;};};
  midi(0x90,69,100);const base=render()(440);assert(base>.01);midi(0xe7,127,127);set('bend_range',12);wasm._web_commit();assert(render()(880)>base*.7,'legacy channel/range preserves active bend');midi(0xb0,123,0);
  set('mpe_mode',1);set('bend_range',2);set('mpe_bend_range',12);wasm._web_commit();midi(0xe1,127,127);midi(0xe2,0,0);midi(0x91,69,100);midi(0x92,69,100);let spectrum=render();assert(spectrum(880)>base*.7&&spectrum(220)>base*.7&&spectrum(440)<base*.1);
  midi(0xe0,127,127);spectrum=render();assert(spectrum(987.7666)>base*.6&&spectrum(246.9416)>base*.6,'master bend adds to member bend');midi(0xe0,0,64);
  const dst=meta.destinations.find(d=>d.id==='op1_level').index;wasm._web_route(0,meta.sources.find(s=>s.id==='at').index,dst,-1);midi(0xd1,127,0);spectrum=render();assert(spectrum(880)<base*.05&&spectrum(220)>base*.7,'pressure isolates one voice');
  midi(0xd0,127,0);spectrum=render();assert(spectrum(880)<base*.05&&spectrum(220)<base*.05,'master pressure sums with member pressure');midi(0xd0,0,0);
  wasm._web_route(0,meta.sources.find(s=>s.id==='timbre').index,dst,-1);midi(0xb1,74,0);midi(0xb2,74,127);spectrum=render();assert(spectrum(880)>base*.7&&spectrum(220)<base*.05,'CC74 isolates one voice');midi(0xb0,74,127);spectrum=render();assert(spectrum(880)<base*.05&&spectrum(220)<base*.05,'master CC74 sums around center');midi(0xb0,74,64);wasm._web_route(0,0,-1,0);
  midi(0xb0,64,127);midi(0x81,69,0);midi(0x82,69,0);render();assert.equal(wasm._web_meter(0),2);midi(0xb0,64,0);render();assert.equal(wasm._web_meter(0),0);
  midi(0x91,69,100);midi(0x92,69,100);midi(0x81,69,0);spectrum=render();assert.equal(wasm._web_meter(0),1);assert(spectrum(220)>base*.7);
  for(const [a,b]of [[101,0],[100,0],[6,2]])midi(0xb2,a,b);midi(0xe2,127,127);assert(render()(493.8833)>base*.7);midi(0xb0,123,0);render();assert.equal(wasm._web_meter(0),0);
  set('mpe_bend_range',12);set('op1_rel',100);wasm._web_commit();midi(0xe1,127,127);midi(0x91,69,100);render();midi(0x81,69,0);midi(0xe1,0,0);midi(0x91,57,100);spectrum=render();assert(spectrum(880)>base*.3&&spectrum(110)>base*.7,'released tails retain expression on channel reuse');
  set('op1_rel',0);wasm._web_commit();midi(0xb0,123,0);midi(0xb0,101,0);midi(0xb0,100,6);midi(0xb0,6,0);midi(0x91,69,100);render();assert.equal(wasm._web_meter(0),0);set('mpe_mode',0);wasm._web_commit();midi(0x90,69,100);assert(render()(440)>base*.7);
  checks.push({rate,oversampling:4,passed:true,scope:'master/member pitch, live legacy range, independent pressure/CC74, duplicate pitch, sustain, RPN sensitivity/zone, panic, mode switch, frozen release tails'});
}
const hash=file=>createHash('sha256').update(readFileSync(new URL(file,import.meta.url))).digest('hex');
const report={wasmSha256:hash('../../build-web/usina.wasm'),moduleSha256:hash('../../build-web/usina.mjs'),checks,passed:true};writeFileSync(new URL('../../build-web/parity/mpe-audio-report.json',import.meta.url),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
