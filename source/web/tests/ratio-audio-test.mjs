// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import create from '../../build-web/usina.mjs';
import {operatorRatioText} from '../synth-parameters.js';
const m=await create(),meta=JSON.parse(m.UTF8ToString(m._web_schema())),ids=new Map(meta.parameters.map((p,i)=>[p.id,i]));
const results=[];
for(const rate of [44100,48000]){
  assert.equal(m._web_init(rate,4),1);const values={},set=(id,v)=>{values[id]=v;m._web_set(ids.get(id),v);};
  set('fx_on',0);set('op1_rel',0);set('op1_coarse',7);set('op1_fine',100);set('op1_sus',100);set('op1_dec',0);m._web_commit();m._web_midi(0x90,69,100);
  function measure(hz){for(let i=0;i<Math.ceil(rate*.15/128);i++)m._web_render(128);const length=8192,samples=new Float32Array(length);for(let at=0;at<length;at+=128){m._web_render(128);samples.set(m.HEAPF32.subarray(m._web_output(0)/4,m._web_output(0)/4+128),at);}assert(samples.every(Number.isFinite));let re=0,im=0;for(let i=0;i<length;i++){const a=2*Math.PI*hz*i/rate;re+=samples[i]*Math.cos(a);im+=samples[i]*Math.sin(a);}const amp=2*Math.hypot(re,im)/length;assert(amp>.01,`missing ${hz} Hz at ${rate}: ${amp}`);assert.equal(m._web_meter(0),1,'parameter commit must preserve the held voice');return amp;}
  const measured=[{hz:440,amplitude:measure(440)}];
  for(const [id,v,hz]of [['op1_coarse',8,880],['op1_fine',150,1320],['op1_trans',12,2640],['op1_detune',-50,2640*2**(-50/1200)]]){const spec=meta.parameters.find(p=>p.id===id);if(v<spec.min||v>spec.max)continue;set(id,v);m._web_commit();measured.push({id,value:v,hz,amplitude:measure(hz)});}
  set('op1_freq',1);set('op1_fixed_hz',44000);m._web_commit();measured.push({hz:440,amplitude:measure(440)});assert.equal(operatorRatioText(id=>values[id]??0,'op1_'),'440.00 Hz');
  // Ratio controls are dimmed in FIXED mode; changing them does not change its frequency.
  set('op1_coarse',10);set('op1_fine',100);set('op1_trans',0);set('op1_detune',0);m._web_commit();measure(440);
  set('op1_freq',0);m._web_commit();measured.push({hz:1760,amplitude:measure(1760)});
  m._web_midi(0x80,69,0);for(let i=0;i<200;i++)m._web_render(128);assert.equal(m._web_meter(0),0);m._web_dispose();results.push({rate,measured});
}
console.log('Live ratio/Fixed WASM audio: coarse/fine/transpose, Fixed Hz independence and return, held-voice continuity and note-off at 44.1/48 kHz passed.');
