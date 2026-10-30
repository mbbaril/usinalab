// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdirSync,existsSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {exportPluginSound,soundFilename} from '../preset-export.js';
import {parseVstSound,applyVstSound} from '../preset-import.js';
const require=createRequire(new URL('../../mobile/package.json',import.meta.url)),{DOMParser}=require('@xmldom/xmldom');
class Parser extends DOMParser{constructor(){super({onError:(_level,message)=>{throw Error(message);}});}}
const meta=JSON.parse(readFileSync(new URL('../../build-web/dist/metadata.json',import.meta.url))),root=new URL('../../',import.meta.url),folder=new URL('build-web/parity/preset-export/',root);
mkdirSync(folder,{recursive:true});
const rig=new Set(['in_gain','in_mix','in_fx','feedback','fb_time','level','ceiling','oversampling','seq_bpm','seq_play','bend_range','mpe_mode','mpe_bend_range']);
const native=process.env.USINA_PRESET_RECEIVER||fileURLToPath(new URL('build-release/UsinaProcessorTest_artefacts/Release/UsinaProcessorTest.exe',root));
const results=[];
for(let variant=0;variant<8;variant++){
 const params=Object.fromEntries(meta.parameters.map((d,i)=>[d.id,variant===0?d.default:variant===1?d.min:variant===2?d.max:Math.min(d.max,d.min+Math.round(((i*17+variant*31)%101)/100*(d.max-d.min)/d.step)*d.step)]));
 const state={preset:'Sino & <metal> "A"',params,routes:Array.from({length:variant===7?64:4},(_,i)=>({source:meta.sources[(i+variant)%meta.sources.length].index,destination:meta.destinations[(i*7+variant)%meta.destinations.length].index,amount:(i%3-1)*.73}))};
 state.functions=Array.from({length:3},(_,k)=>Array.from({length:variant===7?64:3},(_,i)=>{const n=variant===7?64:3;return[i/(n-1),((i*13+k*7)%101)/100,(i%3-1)*.6789];}));
 state.sequence=['preserve chords and durations'];state.ccMaps=[{param:'cutoff',cc:74,channel:-1}];
 const snapshot=JSON.stringify(state),plan=exportPluginSound(state,meta),back=parseVstSound(plan.text,meta,Parser);assert.equal(JSON.stringify(state),snapshot);assert.deepEqual(back.blockers,[]);assert.equal(back.name,state.preset);assert.equal(back.routes.length,state.routes.length);assert.deepEqual(back.routes,state.routes);
 const applied=applyVstSound(state,back,meta);assert.strictEqual(applied.sequence,state.sequence);assert.strictEqual(applied.ccMaps,state.ccMaps);
 for(const d of meta.parameters)if(!rig.has(d.id))assert(Math.abs(applied.params[d.id]-params[d.id])<Math.max(1e-6,d.step*1e-5),d.id);
 const input=new URL(`case-${variant}.usina`,folder),output=new URL(`native-${variant}.usina`,folder),report=new URL(`native-${variant}.json`,folder);writeFileSync(input,plan.text);
 if(existsSync(native)){
  execFileSync(native,['--roundtrip-web-preset',fileURLToPath(input),fileURLToPath(output),fileURLToPath(report)],{timeout:30000,stdio:'pipe'});
  const receipt=JSON.parse(readFileSync(report));assert.equal(receipt.rigPreserved,true);assert.equal(receipt.sequenceEnabledSteps,0);assert.equal(receipt.sequencerOn,0);assert.equal(receipt.routes,state.routes.length);
  const actual=parseVstSound(readFileSync(output,'utf8'),meta,Parser);assert.deepEqual(actual.blockers,[]);assert.deepEqual(actual.functions,back.functions);
  for(const[id,value]of Object.entries(back.params)){assert(id in actual.params,id);assert(Math.abs(value-actual.params[id])<=2e-6*Math.max(1,Math.abs(value)),`${variant}: ${id} ${value} != ${actual.params[id]}`);}
  actual.routes.forEach((r,i)=>{assert.equal(r.source,back.routes[i].source);assert.equal(r.destination,back.routes[i].destination);assert(Math.abs(r.amount-back.routes[i].amount)<1e-6);});
  results.push({variant,parameters:Object.keys(actual.params).length,routes:actual.routes.length,points:actual.functions.map(f=>f.length),rigPreserved:true,sequenceCleared:true});
 }
}
const valid={preset:'Teste',params:Object.fromEntries(meta.parameters.map(d=>[d.id,d.default])),routes:[],functions:[[[0,1,0],[1,0,0]],[[0,0,0],[1,1,0]],[[0,1,0],[1,0,0]]]};
for(const mutate of [s=>s.params.cutoff=NaN,s=>s.params.cutoff=1e9,s=>s.params.unknown=1,s=>s.routes=[{source:999999,destination:4,amount:.5}],s=>s.routes=[{source:1,destination:4,amount:Infinity}],s=>s.functions[0][0][1]=2,s=>{s.functions[0][0][0]=1;s.functions[0][1][0]=.5;},s=>s.functions.length=2,s=>s.preset='bad\u0000name']){const bad=structuredClone(valid);mutate(bad);assert.throws(()=>exportPluginSound(bad,meta));}
for(const [name,want]of [['CON','Usina CON.usina'],['Teste/novo','Teste novo.usina'],['...','Usina.usina']])assert.equal(soundFilename(name),want);
assert(exportPluginSound({...valid,preset:'Som 🎹'},meta).text.includes('Som 🎹'));
const hash=p=>createHash('sha256').update(readFileSync(new URL(p,root))).digest('hex');
writeFileSync(new URL('build-web/parity/preset-export-report.json',root),JSON.stringify({passed:true,nativeReceiverPassed:results.length===8,cases:8,results,exportSha256:hash('web/preset-export.js'),importSha256:hash('web/preset-import.js'),uiSha256:hash('web/preset-export-ui.js'),nativeSourceSha256:hash('src/plugin/PluginProcessor.cpp'),nativeTestSha256:hash('tests/ProcessorTest.cpp'),metadataSha256:hash('build-web/dist/metadata.json')},null,2));
console.log(`Sound export: 8 complete parameter vectors, signed routes, 3 FUNC curves (up to 64 points), validation and no live-state mutation passed; native receiver ${results.length}/8, rig preserved and sequence explicitly cleared.`);
