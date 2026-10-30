// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {touchValue} from '../touch-value.js';
import {SequenceEditor} from '../sequence-model.js';
import {RollHistory} from '../roll-history.js';
const state={sequence:[[{on:true,note:60,notes:[60,64],velocity:90}]],lanes:[{length:1}]},m=new SequenceEditor(state,()=>{}),history=new RollHistory(m),captures=new Set();let snapshot,finishes=0;
const el=()=>({children:[],attrs:{},dataset:{},style:{setProperty(){}},append(...n){this.children.push(...n);},setAttribute(k,v){this.attrs[k]=v;},setPointerCapture:id=>captures.add(id),hasPointerCapture:id=>captures.has(id),releasePointerCapture(id){captures.delete(id);this.onlostpointercapture?.({pointerId:id,type:'lostpointercapture'});}});
const box=touchValue({el,label:'VEL',min:1,max:127,get:()=>m.step.velocity,set:v=>m.edit(0,{velocity:v}),begin:()=>snapshot=history.capture(),finish:()=>{history.commit(snapshot);finishes++;}}),dial=box.children[1],e=(y,type='pointermove')=>({pointerId:4,clientY:y,type,button:0,preventDefault(){}});
dial.onpointerdown(e(100,'pointerdown'));for(let y=101;y<120;y++)dial.onpointermove(e(y));assert.equal(history.past.length,0);dial.onpointerup(e(120,'pointerup'));assert.equal(history.past.length,1,'a drag is one command');const edited=m.step.velocity;history.undo();assert.equal(m.step.velocity,90);assert.deepEqual(m.step.notes,[60,64]);history.redo();assert.equal(m.step.velocity,edited);
dial.onpointerdown(e(100,'pointerdown'));dial.onpointermove(e(80));box.cancelTouchValue();const stopped=m.step.velocity;dial.onpointermove(e(0));assert.equal(m.step.velocity,stopped);assert.equal(captures.size,0);assert.equal(finishes,2,'cancellation finishes once despite lost capture');
console.log('Step editor: one Undo command per vertical gesture, exact velocity/chord restore and pause capture cleanup passed.');
