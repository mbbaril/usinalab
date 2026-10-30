// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import create from '../../build-web/filter-analyzer.mjs';
import {cutoffHz,filterPoint} from '../filter-response.js';
assert.equal(cutoffHz(0),6.489);assert(Math.abs(cutoffHz(1000)-23680)<1e-8);
const rect={left:10,top:20,width:400,height:200};
assert.deepEqual(filterPoint(210,120,rect),{cutoff:500,reso:50});
assert.deepEqual(filterPoint(-100,999,rect),{cutoff:0,reso:0});
assert.deepEqual(filterPoint(999,-10,rect),{cutoff:1000,reso:100});
const m=await create(),rate=192000,cut=600,res=45;
function measure(type,options={}){const status=m._filter_measure(type,options.cut??cut,options.res??res,options.drive??0,200,30,options.fb??0,options.hot??0,options.life??0,rate,0);return {status,values:Array.from(m.HEAPF32.slice(m._filter_result()/4,m._filter_result()/4+321))};}
let checks=0;
for(let type=0;type<18;type++){const a=measure(type);assert.equal(a.status,0,`settled type ${type}`);assert(a.values.every(Number.isFinite));const b=measure(type);assert.deepEqual(a,b,`repeatable type ${type}`);checks++;}
// Independent analytic response of the linear SVF family, including the second Butterworth stage.
for(const type of [0,1,4,5,7,8,9,10]){
  const a=measure(type).values,two=[1,5,8,10].includes(type),kind=type<4?'lp':type<7?'hp':type<9?'bp':'br',g=Math.tan(Math.PI*cutoffHz(cut)/rate),ks=two?[0.765367-res*.00735357,1.847759]:[1.414214-res*.0138419];
  for(let i=140;i<280;i+=7){const hz=cutoffHz(i/320*1000),w=Math.tan(Math.PI*hz/rate);let mag=1;
    for(const k of ks){const numerator=kind==='lp'?g*g:kind==='hp'?w*w:kind==='bp'?k*g*w:Math.abs(g*g-w*w);mag*=numerator/Math.hypot(g*g-w*w,k*g*w);}
    const expected=20*Math.log10(Math.max(mag,1e-8));if(expected>-65){assert(Math.abs(a[i]-expected)<.18,`type ${type} at ${hz}: ${a[i]} vs ${expected}`);checks++;}
  }
}
assert(measure(0,{res:80}).values[192]>measure(0,{res:0}).values[192]+6,'resonance peak increases');
const casc=measure(11).values,lp=measure(2).values;assert(casc[20]<lp[20]-12,'cascade includes the high-pass boundary');
assert(Math.abs(measure(11,{drive:100}).values[180]-casc[180]-20*Math.log10(8))<.01,'cascade input drive follows the native gain');
assert.equal(measure(14,{res:100,life:100}).status,1,'do not show a non-stationary CMOS transient as a transfer curve');
assert.equal(m._filter_measure(0,600,45,0,200,30,0,0,0,44100,1),0);
const firmware=m.HEAPF32.slice(m._filter_result()/4,m._filter_result()/4+321);
assert(Number.isFinite(firmware[310])&&Number.isNaN(firmware[320]),'omit response bins beyond Nyquist in 1x mode');
const hash=p=>createHash('sha256').update(readFileSync(new URL(p,import.meta.url))).digest('hex');
const report={passed:true,checks,nativeFilterSha256:hash('../../src/engines/fm/FmFilters.h'),nativeVoiceFilterSha256:hash('../../src/engines/fm/FmVoiceFilter.h'),measurementSourceSha256:hash('../filter-response.cpp'),analyzerSha256:hash('../../build-web/filter-analyzer.wasm'),analyzerGlueSha256:hash('../../build-web/filter-analyzer.mjs'),viewSha256:hash('../filter-response.js'),animationSha256:hash('../response-curve.js'),workerSha256:hash('../filter-response-worker.js'),scope:'Display-only small-signal impulse response of unchanged native kernels; SVF analytic comparison, 18 topologies, cascade HP boundary, non-stationary response rejected. Does not measure the complete modulated audio chain.'};
writeFileSync(new URL('../../build-web/parity/filter-response-report.json',import.meta.url),JSON.stringify(report,null,2));
console.log(`Filter response: ${checks} native-kernel/analytic checks, XY mapping and non-stationary rejection passed.`);
