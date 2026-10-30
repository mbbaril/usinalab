// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';import {writeFileSync,readFileSync} from 'node:fs';import {createHash} from 'node:crypto';import create from '../../build-web/usina.mjs';
const m=await create(),meta=JSON.parse(m.UTF8ToString(m._web_schema())),ids=new Map(meta.parameters.map((d,i)=>[d.id,i])),slots=['fx_a','fx_b','aux','loop'].flatMap(b=>['','2','3'].map(s=>`${b}${s}_type`)),cases=[];
for(let effect=0;effect<17;effect++)cases.push({name:`operator EFFECT ${effect}`,values:{fx_on:0,op1_mode:5,op1_a:effect,op1_b:100,op1_c:100,op1_d:40}});
for(const kind of [1,2,3,5,6])for(const slot of slots)cases.push({name:`slot ${slot} kind ${kind}`,values:{[slot]:kind}});
for(let media=0;media<4;media++)cases.push({name:`TEXTURE media ${media}`,values:{fx_a_type:1,tex_media:media}});
for(let color=0;color<2;color++)cases.push({name:`ROOM color ${color}`,values:{fx_a_type:2,rev_color:color}});
for(let mode=0;mode<9;mode++)cases.push({name:`DISTORT mode ${mode}`,values:{fx_a_type:3,dist_mode:mode}});
for(let mode=0;mode<6;mode++)for(let quality=0;quality<4;quality++)cases.push({name:`VAPOR mode ${mode} quality ${quality}`,values:{fx_a_type:5,vap_mode:mode,vap_quality:quality}});
for(let character=0;character<2;character++)for(let routing=0;routing<3;routing++)cases.push({name:`DELAY character ${character} routing ${routing}`,values:{fx_a_type:6,dly_character:character,dly_routing:routing}});
const results=[];
for(const rate of [44100,48000])for(const test of cases){
 m._web_seed(12345);assert.equal(m._web_init(rate,4),1);const set=(id,v)=>{assert(ids.has(id),id);m._web_set(ids.get(id),v);};
 for(const slot of slots)set(slot,0);for(const[id,value]of Object.entries({op1_rel:0,op1_dec:0,op1_sus:100,fx_on:1,tex_on:1,tex_drywet:.5,rev_on:1,rev_amount:.3,dist_on:1,dist_mix:.5,vap_on:1,vap_drywet:.5,dly_on:1,dly_time:30,dly_mix:.5,aux_send:.6,feedback:.1,fb_time:23,...test.values}))set(id,value);
 m._web_commit();let energy=0,peak=0;
 for(let frame=0;frame<8192;frame+=128){if(!frame)m._web_midi(0x90,69,100);if(frame===4096)m._web_midi(0x80,69,0);m._web_render(128);for(let ch=0;ch<2;ch++){const p=m._web_output(ch)/4;for(let i=0;i<128;i++){const value=m.HEAPF32[p+i];assert(Number.isFinite(value),`${test.name} at ${rate}`);energy+=value*value;peak=Math.max(peak,Math.abs(value));}}}
 assert(energy>1e-12,`${test.name} unexpectedly silent at ${rate}`);assert.equal(m._web_meter(0),0,`${test.name}: stuck voice`);results.push({rate,name:test.name,energy,peak,runaways:m._web_meter(3)});m._web_dispose();
}
const sha=p=>createHash('sha256').update(readFileSync(new URL(p,import.meta.url))).digest('hex');writeFileSync(new URL('../../build-web/parity/effects-audio-report.json',import.meta.url),JSON.stringify({passed:true,cases:results.length,framesPerCase:8192,wasmSha256:sha('../../build-web/usina.wasm'),moduleSha256:sha('../../build-web/usina.mjs'),scope:'Smoke tests of finite/nonzero output and note release in unchanged playback WASM. 17 operator effects; all five supported slot families in all 12 slots; every standalone mode/quality/media/color/routing. This is not a native-vs-WASM sound comparison, a timbral assertion or physical performance measurement.',results},null,2));console.log(`${results.length} effect audio smoke renders at 44.1/48 kHz: finite nonzero output, all note releases; max peak ${Math.max(...results.map(r=>r.peak))}; guard events ${results.reduce((n,r)=>n+r.runaways,0)}. DSP unchanged.`);
