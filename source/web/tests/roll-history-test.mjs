// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {SequenceEditor} from '../sequence-model.js';
import {RollHistory} from '../roll-history.js';
const make=()=>Array.from({length:64},()=>({on:true,note:60,notes:[60,64],durations:{60:2,64:1},velocity:90})),state={sequence:[make(),make()],lanes:[{length:64},{length:64}]},sent=[];
const model=new SequenceEditor(state,(l,i)=>sent.push([l,i])),history=new RollHistory(model),original=JSON.stringify(state.sequence[0]);
history.run(()=>{model.write(model.steps[0],[]);model.edit(0,{});model.write(model.steps[3],[67]);model.edit(3,{});});assert(history.canUndo());sent.length=0;assert(history.undo());assert.equal(JSON.stringify(state.sequence[0]),original);assert.deepEqual(sent,[[0,0],[0,3]],'only changed steps published, no player reset');assert(history.redo());assert(!model.steps[0].on);
history.undo();model.lane=1;assert(!history.canUndo(),'histories are per lane');history.run(()=>model.edit(1,{velocity:50}));model.lane=0;assert(history.canRedo());history.redo();history.run(()=>model.edit(8,{velocity:80}));assert(!history.canRedo(),'new edit drops redo branch');
model.edit(8,{velocity:40});assert(!history.canUndo(),'external edits are protected');
state.sequence=[make(),make()];assert(!history.canUndo());assert(!history.canRedo(),'session replacement invalidates history');
for(let i=0;i<100;i++)history.run(()=>model.edit(0,{velocity:i}));assert.equal(history.past.length,64,'bounded action history');
console.log('Roll history: grouped step deltas, exact chord/durations, per-lane Undo/Redo, branch invalidation, session replacement and bounded memory passed.');
