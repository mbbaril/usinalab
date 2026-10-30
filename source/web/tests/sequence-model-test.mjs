import assert from 'node:assert/strict';
import {SequenceEditor,noteName,notesOf,writeNotes} from '../sequence-model.js';
const blank=()=>({on:false,note:60,velocity:100,gate:.5,probability:1,ratchet:1,inherit:true});
const state={sequence:Array.from({length:9},()=>Array.from({length:64},blank)),lanes:Array.from({length:9},()=>({length:32}))},sent=[];
const m=new SequenceEditor(state,(l,i,s)=>sent.push([l,i,[...notesOf(s)]]));
assert.equal(noteName(60),'C4');assert.equal(noteName(0),'C-1');assert.equal(noteName(127),'G9');
m.midi(0x90,60,100);assert.equal(sent.length,0);m.midi(0x80,60,0);m.armed=true;m.selected=15;m.midi(0x90,60,100);m.midi(0x91,64,110);m.midi(0x92,67,90);
assert.deepEqual(m.steps[15].notes,[60,64,67]);m.midi(0x80,60,0);assert.equal(m.selected,15);m.midi(0x81,64,0);m.midi(0x92,67,0);assert.equal(m.selected,16);assert.equal(m.page,1);
m.midi(0x90,72,100);m.midi(0x80,72,0);assert.deepEqual(m.steps[16].notes,[72]);m.move(15,64,16,76);assert.deepEqual(m.steps[15].notes,[60,67]);assert.deepEqual(m.steps[16].notes,[72,76]);
m.move(15,60,16,72);assert.deepEqual(m.steps[15].notes,[67]);assert.deepEqual(m.steps[16].notes,[72,76]);
m.cancel();m.midi(0x90,80,100);assert.equal(m.held.size,0);writeNotes(m.steps[0],[0,0,127]);assert.deepEqual(m.steps[0].notes,[0,127]);
m.mode='roll';m.setLane(1);assert.equal(m.mode,'steps');m.armed=true;m.midi(0x90,60,100);assert.equal(m.steps[0].on,false);
console.log('Sequence model: chord capture, all-note release advance, page boundary, polyphonic merge, duplicate pitch, limits and cancellation pass');

assert.deepEqual(JSON.parse(JSON.stringify(state)).sequence[0][16].notes,[72,76]);
