// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {SequenceEditor} from '../sequence-model.js';
import {RollHistory} from '../roll-history.js';
import {createSequenceGenerator} from '../sequence-generator.js';
let worker;globalThis.Worker=class{constructor(){worker=this;}postMessage(request){this.request=structuredClone(request);}terminate(){}};
const make=()=>Array.from({length:64},()=>({on:false,note:60,notes:[60],velocity:90})),state={sequence:[make(),make()],lanes:[{length:64},{length:64}]},sent=[],messages=[],m=new SequenceEditor(state,(l,i,s)=>sent.push({l,i,s})),h=new RollHistory(m);
const g=createSequenceGenerator({state,send:v=>sent.push(v),message:v=>messages.push(v),changed(){}});
function start(operation){const snapshot=h.capture();g.run(m.lane,operation,{},action=>{action();h.commit(snapshot);});}
function respond(steps){worker.onmessage({data:{id:worker.request.id,steps}});}
for(const operation of [0,1,2,3,4,5,6]){const before=JSON.stringify(m.steps),steps=structuredClone(m.steps);steps[operation]={on:true,note:60+operation,notes:[60+operation,64+operation],durations:{[60+operation]:2,[64+operation]:1},velocity:70};start(operation);respond(steps);const after=JSON.stringify(m.steps);assert(h.undo());assert.equal(JSON.stringify(m.steps),before);assert(h.redo());assert.equal(JSON.stringify(m.steps),after,'redo reuses exact random results');}
// A result belongs to its requested lane, even after navigating elsewhere.
m.lane=0;const before=JSON.stringify(m.steps);start(1);const result=structuredClone(m.steps);result[9].on=true;m.lane=1;respond(result);assert(!h.canUndo());m.lane=0;assert(h.undo());assert.equal(JSON.stringify(m.steps),before);
start(1);m.edit(0,{velocity:20});const latest=JSON.stringify(m.steps);respond(result);assert.equal(JSON.stringify(m.steps),latest);assert(messages.at(-1).includes('mudou'));
start(1);state.sequence=structuredClone(state.sequence);respond(result);assert(!h.canUndo());
console.log('Generation history: all seven commands, exact random Redo, original lane after navigation, stale edits and session identity protection passed.');
