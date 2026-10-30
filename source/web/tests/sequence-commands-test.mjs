// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import create from '../../build-web/usina.mjs';
import {encodeSequence,decodeLane,wireSize,reconcileChords,conditionNames} from '../sequence-wire.js';
const state={lanes:Array.from({length:9},(_,l)=>({length:16,direction:0,mul:1,div:1,mute:false,baseNote:l>=5?[36,41,48,55][l-5]:60})),sequence:Array.from({length:9},()=>Array.from({length:64},(_,i)=>({on:i%3===0,note:60+i%7,velocity:80+i%30,probability:.75,ratchet:1,gate:.5,accent:.2,late:0,condition:0,pitchOffset:0,inherit:false})))};
const fixture=readFileSync(new URL('../../build-web/parity/native-commands.f32',import.meta.url));const native=new Float32Array(fixture.buffer,fixture.byteOffset,fixture.length/4);assert.equal(native.length,wireSize*6);
const m=await create(),wire=new Float32Array(m.HEAPF32.buffer,m._web_sequence_buffer(),wireSize);let maxError=0;
for(let operation=0;operation<6;operation++){encodeSequence(state,wire);assert.equal(m._web_sequence_command(0,operation,5,3,.4,11),1);for(let i=0;i<wireSize;i++){const error=Math.abs(wire[i]-native[operation*wireSize+i]);maxError=Math.max(maxError,error);assert(error<1e-6,`Native/WASM command ${operation}, field ${i}: ${wire[i]} vs ${native[operation*wireSize+i]}`);}}
assert.equal(m._web_sequence_command(-1,0,4,0,.4,1),0);
const before=structuredClone(state.sequence[0]);before[0].notes=[60,64,67];const after=structuredClone(before);after[0].note=65;reconcileChords(before,after,1,'intervals',16);assert.deepEqual(after[0].notes,[65,69,72]);reconcileChords(before,after,1,'mono',16);assert.equal(after[0].notes,undefined);
assert.throws(()=>reconcileChords(before,structuredClone(before),1,'',16));const rotated=structuredClone(before);reconcileChords(before,rotated,5,'',16);assert.deepEqual(rotated[1].notes,[60,64,67]);
assert.equal(conditionNames.length,11);assert.equal(conditionNames.at(-1),'1ST');
const hash=file=>createHash('sha256').update(readFileSync(new URL(file,import.meta.url))).digest('hex');const report={passed:true,commands:6,fields:wireSize*6,maxError,comparison:'shared VST command helper, MSVC native fixture vs WASM',nativeFixtureSha256:createHash('sha256').update(fixture).digest('hex'),wasmSha256:hash('../../build-web/usina.wasm'),moduleSha256:hash('../../build-web/usina.mjs')};writeFileSync(new URL('../../build-web/parity/sequence-commands-report.json',import.meta.url),JSON.stringify(report,null,2));console.log(JSON.stringify(report));

const high=structuredClone(before),moved=structuredClone(before);high[0].note=120;high[0].notes=[120,124,127];moved[0].note=127;moved[0].pitchOffset=10;reconcileChords(high,moved,1,'intervals',16);assert.deepEqual(moved[0].notes,[120,124,127]);assert.equal(moved[0].pitchOffset,0);

const schema=JSON.parse(m.UTF8ToString(m._web_schema()));
for(const scale of [1,15,31])for(const root of [0,4,11])for(const operation of [1,2,6]){
  state.lanes[0].scale=scale;state.lanes[0].root=root;encodeSequence(state,wire);assert.equal(m._web_sequence_command(0,operation,4,0,.8,7),1);const notes=decodeLane(wire,0,state.sequence[0]);for(const s of notes.slice(0,16)){assert.equal(s.pitchOffset,0);assert.equal(m._web_quantize(scale,root,s.note),s.note);}
}
console.log('Selected-scale RANDOM/MUTATE/APPLY use concrete on-scale pitches without a second transposition');
