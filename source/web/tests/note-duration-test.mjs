// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {explicitDuration,noteDuration,mappedDurations,validDurations} from '../note-duration.js';
import {SequenceEditor,writeNotes} from '../sequence-model.js';
import {reconcileChords} from '../sequence-wire.js';
const old={on:true,note:60,notes:[60,64],gate:.5,ratchet:2};assert.equal(noteDuration(old,60),.25);assert.equal(noteDuration(old,60,{mul:2,div:1}),.5);
const s={...old,durations:{60:3.5,64:1}};assert.equal(noteDuration(s,60),3.5);assert.equal(noteDuration(s,64),1);assert.equal(explicitDuration(old,60),null);
assert.deepEqual(mappedDurations(s,[60,64],[62,62]),{62:3.5});writeNotes(s,[64]);assert.deepEqual(s.durations,{64:1});
const state={lanes:[{length:64}],sequence:[Array.from({length:64},()=>({...old,on:false,notes:[60]}))]},model=new SequenceEditor(state,()=>{});
state.sequence[0][0]={...old,durations:{60:4,64:2}};model.move(0,60,1,62);assert.deepEqual(state.sequence[0][0].durations,{64:2});assert.deepEqual(state.sequence[0][1].durations,{62:4});model.move(1,62,1,65);assert.deepEqual(state.sequence[0][1].durations,{65:4});
const before=state.sequence[0],after=structuredClone(before);reconcileChords(before,after,5,'intervals',16);assert.deepEqual(after[2].durations,{65:4});
const gen=structuredClone(before);gen[1].note=67;reconcileChords(before,gen,1,'intervals',16);assert.deepEqual(gen[1].durations,{67:4});
for(const v of [undefined,{}, {'0':.01,'127':64}])assert(validDurations(v));for(const v of [null,[],{'128':1},{'60':Infinity},{'60':0},{'60':65},{'01':1}])assert(!validDurations(v));
console.log('Note durations: legacy inheritance, clock units, independent lengths, transpose/merge/rotation and strict session validation passed.');
