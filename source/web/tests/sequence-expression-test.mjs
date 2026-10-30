// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import create from '../../build-web/usina.mjs';
const m=await create(),schema=JSON.parse(m.UTF8ToString(m._web_schema())),ids=new Map(schema.parameters.map((d,i)=>[d.id,i]));
for(const fs of [44100,48000]){
  assert.equal(m._web_init(fs,4),1);const set=(id,v)=>m._web_set(ids.get(id),v);set('fx_on',0);set('op1_rel',0);set('seq_on',1);set('seq_play',1);set('seq_bpm',120);m._web_commit();
  m._web_lane(0,2,0,1,1,0);m._web_lane_base(0,55);m._web_step(0,0,1,60,30,1,1,1);m._web_step_expression(0,0,1,50,1,0,1);m._web_flush_edits();
  let frames=0;const until=seconds=>{while(frames<Math.floor(seconds*fs)){const n=Math.min(128,Math.floor(seconds*fs)-frames);m._web_render(n);frames+=n;}};
  until(.04);assert.equal(m._web_meter(0),0,'LATE must delay the first note');until(.06);assert.equal(m._web_meter(0),1,'Delayed inherited note must begin');
  const data=new Float32Array(2048);for(let i=0;i<2048;i+=128){m._web_render(128);frames+=128;data.set(m.HEAPF32.subarray(m._web_output(0)/4,m._web_output(0)/4+128),i);}
  const amp=hz=>{let re=0,im=0;for(let i=0;i<data.length;i++){const a=2*Math.PI*hz*i/fs;re+=data[i]*Math.cos(a);im+=data[i]*Math.sin(a);}return 2*Math.hypot(re,im)/data.length;};assert(amp(195.9977)>.01&&amp(195.9977)>amp(261.6256)*3,'BASE must supply G3 instead of explicit C4');
  until(.22);assert.equal(m._web_meter(0),0,'100% gate still has a note off');until(.4);assert.equal(m._web_meter(0),0,'Condition 1:2 skips the next loop');until(.56);assert.equal(m._web_meter(0),1,'Condition 1:2 returns on the following loop');
  set('seq_play',0);m._web_commit();until(.9);assert.equal(m._web_meter(0),0);m._web_dispose();
}
console.log('WASM sequence expression: BASE inheritance, LATE, 100% gate, 1:2 condition and stop at 44.1/48 kHz pass');
