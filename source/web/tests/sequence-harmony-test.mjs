// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';import {createHash} from 'node:crypto';
import create from '../../build-web/usina.mjs';
import {quantizeNote,inScale,nextScaleNote,transposeChord} from '../sequence-harmony.js';
import {SequenceEditor} from '../sequence-model.js';
const m=await create(),schema=JSON.parse(m.UTF8ToString(m._web_schema())),scales=schema.sequenceScales;assert.equal(scales.length,32);let checks=0;
for(let type=0;type<32;type++)for(let root=0;root<12;root++)for(let note=0;note<128;note++){
  const q=quantizeNote(note,scales[type],root);assert.equal(q,m._web_quantize(type,root,note),`${type}/${root}/${note}`);assert(inScale(q,scales[type],root));checks++;
}
const major=scales[1];assert.equal(nextScaleNote(60,1,major),62);assert.equal(nextScaleNote(62,-1,major),60);assert.equal(nextScaleNote(127,1,major),127);
assert.deepEqual(transposeChord([60,64,67],60,61,major),{note:60,notes:[60,64,67]});
const state={sequence:Array.from({length:9},()=>Array.from({length:64},()=>({on:false,note:60,velocity:100}))),lanes:Array.from({length:9},()=>({length:16,mono:true}))},edits=[];
const e=new SequenceEditor(state,(_l,i,s)=>edits.push([i,[...s.notes]]));e.quantize=n=>quantizeNote(n,major);e.armed=true;
assert.deepEqual(e.midi(0x90,61,100),[0x90,60,100]);assert.equal(e.midi(0x90,64,100),false);assert.deepEqual(e.steps[0].notes,[60]);assert.deepEqual(e.midi(0x80,61,0),[0x80,60,0]);assert.equal(e.selected,0);assert.equal(e.midi(0x80,64,0),false);assert.equal(e.selected,1);
e.settings.mono=false;e.armed=true;assert.deepEqual(e.midi(0x90,61,100),[0x90,60,100]);assert.equal(e.midi(0x90,60,100),false);assert.deepEqual(e.midi(0xa0,61,90),[0xa0,60,90]);assert.equal(e.midi(0x80,61,0),false);assert.deepEqual(e.midi(0x80,60,0),[0x80,60,0]);
e.armed=true;e.midi(0x90,61,100);e.setLane(2);assert.deepEqual(e.midi(0x80,61,0,false),[0x80,60,0]);
assert.deepEqual(e.midi(0x90,60,100,false,'keybed'),[0x90,60,100]);assert.equal(e.midi(0x90,60,100,false,'input'),false);assert.equal(e.midi(0x80,60,0,false,'keybed'),false);assert.deepEqual(e.midi(0x80,60,0,false,'input'),[0x80,60,0]);
e.resetInput();assert.equal(e.routes.size,0);assert.equal(e.counts.size,0);console.log(`${checks} native scale/root/pitch checks; MONO filtering, quantized collisions/pressure, frozen note-off mapping and input ownership pass`);



e.configureMpe(true);e.midi(0x91,61,100);e.midi(0x92,64,100);e.midi(0xb1,123,0);assert.equal(e.counts.has('1:61'),false);assert(e.counts.has('2:64'));assert.equal(e.midi(0x81,61,0),false);assert.deepEqual(e.midi(0x82,64,0),[0x82,64,0]);
e.midi(0xb0,101,0);e.midi(0xb0,100,6);e.midi(0xb0,6,0);assert.equal(e.members,0);e.midi(0x91,60,100);e.midi(0xb1,123,0);assert.equal(e.routes.size,0);assert.equal(e.counts.size,0);
e.configureMpe(true,true);e.midi(0x91,60,100);e.midi(0x91,62,100);assert(!e.counts.has('1:60'));assert.equal(e.midi(0x81,60,0),false);assert.deepEqual(e.midi(0x81,62,0),[0x81,62,0]);
console.log('Capture router follows native MPE member panic, RPN zone changes, implicit member reuse and global panic');

const hash=file=>createHash('sha256').update(readFileSync(new URL(file,import.meta.url))).digest('hex');writeFileSync(new URL('../../build-web/parity/sequence-harmony-report.json',import.meta.url),JSON.stringify({passed:true,scales:32,checks,comparison:'UI quantizer vs RuidoCore MIDI quantizer in WASM',wasmSha256:hash('../../build-web/usina.wasm'),moduleSha256:hash('../../build-web/usina.mjs')},null,2));
