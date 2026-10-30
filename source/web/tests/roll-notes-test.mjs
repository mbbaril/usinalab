// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {SequenceEditor} from '../sequence-model.js';
import {captureNotes,transferNotes,resizeNotes,durationDelta} from '../roll-notes.js';
import {RollHistory} from '../roll-history.js';
const fresh=()=>({on:false,note:60,notes:[60],velocity:90,probability:1,ratchet:1,gate:.5,accent:0,late:0,condition:0});
const state={sequence:[Array.from({length:64},fresh)],lanes:[{length:64}]},sent=[],m=new SequenceEditor(state,(l,i)=>sent.push(i));
m.write(m.steps[0],[60,64],{60:2,64:1});m.steps[0].velocity=70;m.steps[0].accent=.6;m.steps[0].late=12;
const selected=new Set(['0:60','0:64']),notes=captureNotes(m,selected),history=new RollHistory(m);
assert.equal(notes.length,2);assert.deepEqual(notes.map(n=>n.duration),[2,1]);
let r=history.run(()=>transferNotes(m,notes,{deltaStep:3}));assert(r.ok);assert.deepEqual(m.steps[3].notes,[60,64]);assert.deepEqual(m.steps[3].durations,{60:2,64:1});assert.equal(m.steps[3].velocity,70);assert.equal(m.steps[3].late,12);assert.equal(m.steps[3].accent,.6);history.undo();assert(!m.steps[3].on);history.redo();
const before=JSON.stringify(state);r=transferNotes(m,notes,{deltaStep:3});assert(!r.ok);assert.equal(JSON.stringify(state),before,'same pitch collision never overwrites');
r=history.run(()=>transferNotes(m,notes,{move:true,deltaStep:1,deltaPitch:2}));assert(r.ok);assert(!m.steps[0].on);assert.deepEqual(m.steps[1].notes,[62,66]);assert.deepEqual(m.steps[1].durations,{62:2,66:1});history.undo();assert.deepEqual(m.steps[0].notes,[60,64]);
for(const options of [{move:true,deltaStep:-1},{move:true,deltaStep:64},{deltaPitch:80}]){const b=JSON.stringify(state);assert(!transferNotes(m,notes,options).ok);assert.equal(JSON.stringify(state),b,'out-of-bounds operation is atomic');}
m.write(m.steps[2],[72]);m.steps[2].velocity=100;const conflict=JSON.stringify(state);assert(!transferNotes(m,notes,{deltaStep:2}).ok);assert.equal(JSON.stringify(state),conflict,'shared velocity/expression conflict rejected');
assert(transferNotes(m,notes,{deltaStep:2,policy:'destination'}).ok);assert.equal(m.steps[2].velocity,100);assert(m.steps[2].notes.includes(72));
// Selected notes may move over each other's old positions after all origins are removed.
m.write(m.steps[5],[50]);m.write(m.steps[6],[50]);const chain=captureNotes(m,new Set(['5:50','6:50']));assert(transferNotes(m,chain,{move:true,deltaStep:1}).ok);assert(!m.steps[5].on);assert(m.steps[6].on&&m.steps[7].on);
const old=captureNotes(m,new Set(['0:60']));m.steps[0].velocity=42;const changed=JSON.stringify(state);assert(!transferNotes(m,old,{move:true,deltaStep:4}).ok);assert.equal(JSON.stringify(state),changed,'do not commit a gesture over a changed source');
console.log('Roll clipboard/group operations: exact chords/durations/expression, atomic bounds/collisions/conflicts, overlapping origins, stale-source guard and Undo/Redo passed.');

// Group resize preserves offsets, never partially changes stale/bounded groups.
m.write(m.steps[10],[60,64],{60:2,64:1});m.write(m.steps[11],[67],{67:3});
const group=captureNotes(m,new Set(['10:60','10:64','11:67'])),initial=JSON.stringify(state);
assert.equal(durationDelta(group,-10),-.99);assert.equal(durationDelta(group,100),61);
assert(history.run(()=>resizeNotes(m,group,.5)).ok);assert.deepEqual(m.steps[10].durations,{60:2.5,64:1.5});assert.equal(m.steps[11].durations[67],3.5);
assert(history.undo());assert.equal(JSON.stringify(state),initial);assert(history.redo());
const stale=JSON.stringify(state);assert(!resizeNotes(m,group,1).ok);assert.equal(JSON.stringify(state),stale);
const current=captureNotes(m,new Set(['10:60','10:64','11:67']));assert(!resizeNotes(m,current,64).ok);assert.equal(JSON.stringify(state),stale);
console.log('Group resize: shared delta, exact duration differences, atomic stale/bounds refusal and one-step Undo/Redo passed.');
