// SPDX-License-Identifier: GPL-3.0-only
import {mkdirSync,writeFileSync,readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import create from '../../build-web/usina.mjs';
const output=new URL('../../build-web/android-audio-proof/proof/',import.meta.url);mkdirSync(output,{recursive:true});
const m=await create(),metadata=JSON.parse(m.UTF8ToString(m._web_schema())),ids=new Map(metadata.parameters.map((p,i)=>[p.id,i]));
const allSlots=['fx_a','fx_b','aux','loop'].flatMap(bus=>['','2','3'].map(s=>`${bus}${s}_type`));
const definitions=[{name:'sine',values:{fx_on:0}},{name:'ratio',values:{fx_on:0,op1_coarse:9,op1_fine:137}},
  {name:'filter',values:{fx_on:0,filter:1,cutoff:700,reso:45}},{name:'MPE',values:{fx_on:0,mpe_mode:1,mpe_bend_range:48},
    events:[0,0x91,60,100,960,0xe1,0,96,1700,0xd1,100,0,2300,0xb1,74,110,4096,0x81,60,0]}];
for(const [name,values] of [['room',{fx_a_type:2,rev_on:1,rev_amount:.3}],
  ['distort',{fx_a_type:3,dist_on:1,dist_mix:.5,dist_drive:12}],
  ['delay',{fx_a_type:6,dly_on:1,dly_time:20,dly_mix:.5,dly_wow:0,dly_flutter:0}]])definitions.push({name,values});
const cases=[];
for(const rate of [44100,48000])for(const oversampling of [1,2,4])for(const definition of definitions){
  m._web_seed(12345);if(!m._web_init(rate,oversampling))throw Error('init');
  const values={...Object.fromEntries(allSlots.map(id=>[id,0])),op1_rel:0,op1_dec:0,op1_sus:100,...definition.values};
  const pairs=[];for(const [id,value] of Object.entries(values)){if(!ids.has(id))throw Error(`Unknown parameter ${id}`);pairs.push(ids.get(id),value);m._web_set(ids.get(id),value);}m._web_commit();
  const frames=8192,events=definition.events||[0,0x90,60,100,4096,0x80,60,0],samples=new Float32Array(frames*2);let event=0;
  for(let at=0;at<frames;){
    while(event<events.length&&events[event]<=at){m._web_midi(...events.slice(event+1,event+4));event+=4;}
    const n=Math.min(128,frames-at,event<events.length?events[event]-at:frames-at);m._web_render(n);
    const l=m._web_output(0)/4,r=m._web_output(1)/4;
    for(let i=0;i<n;i++){samples[2*(at+i)]=m.HEAPF32[l+i];samples[2*(at+i)+1]=m.HEAPF32[r+i];}at+=n;
  }
  if(!samples.every(Number.isFinite)||!samples.some(v=>Math.abs(v)>.001))throw Error(`Invalid reference ${definition.name}`);
  const file=`${cases.length}.f32`;writeFileSync(new URL(file,output),new Uint8Array(samples.buffer));
  cases.push({name:definition.name,rate,oversampling,frames,pairs,events,file});m._web_dispose();
}
const sha=file=>createHash('sha256').update(readFileSync(new URL(file,import.meta.url))).digest('hex');
writeFileSync(new URL('manifest.json',output),JSON.stringify({format:1,wasmSha256:sha('../../build-web/usina.wasm'),cases},null,2));
console.log(`${cases.length} deterministic Android/WASM reference renders prepared.`);
