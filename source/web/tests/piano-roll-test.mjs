// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {RollViewport,createPianoRoll} from '../piano-roll.js';
import {SequenceEditor} from '../sequence-model.js';
const v=new RollViewport(16,48);v.span=16;v.rows=13;
const initial={view:{...v},anchor:v.point(.5,.5),dx:.3,dy:.3};
v.navigate(initial,{x:.2,y:.2},{x:.8,y:.8},64);
assert(Math.abs(v.span-8)<1e-9);assert(Math.abs(v.rows-6.5)<1e-9);assert.deepEqual(v.point(.5,.5),initial.anchor);
v.navigate(initial,{x:.1,y:.1},{x:.4,y:.4},64);assert(Math.abs(v.span-16)<1e-9);assert(Math.abs(v.start-20)<1e-9);assert(Math.abs(v.low-44.75)<1e-9);
v.start=-100;v.low=500;v.span=100;v.constrain(1);assert.equal(v.span,1);assert.equal(v.start,0);assert(v.low+v.rows<=128);
class Element{
  constructor(tag,cls='',text=''){this.tag=tag;this.cls=cls;this.textContent=text;this.children=[];this.dataset={};this.style={setProperty(){}};this.classList={add(){},toggle(){}};this.captured=new Set();}
  append(...nodes){this.children.push(...nodes);}replaceChildren(...nodes){this.children=nodes;}setAttribute(){}
  get firstChild(){return this.children[0];}closest(selector){return selector==='.roll-note'&&this.cls==='roll-note'?this:null;}
  getBoundingClientRect(){if(this.cls==='roll-note')return {left:0,right:25,width:25,top:0,height:20};return {left:0,top:0,width:800,height:260};}
  setPointerCapture(id){this.captured.add(id);}hasPointerCapture(id){return this.captured.has(id);}releasePointerCapture(id){this.captured.delete(id);}
  querySelectorAll(selector){const match=selector.match(/data-step="(\d+)"/);return this.children.filter(n=>!match||n.dataset.step===Number(match[1]));}
}
const el=(...args)=>new Element(...args),state={lanes:[{length:64}],sequence:[Array.from({length:64},()=>({on:false,note:60,notes:[60],gate:.5,velocity:100}))]},published=[];
state.sequence[0][0].on=true;state.sequence[0][0].notes=[60,64];
const model=new SequenceEditor(state,(lane,i)=>published.push(i)),host=el('div'),tools=el('div'),view=new RollViewport(0,55);
const c=createPianoRoll({el,host,tools,model,view,scale:()=>null,root:()=>0,changed(){},cursors:()=>[],playing:()=>false,selectPitch(){},shownNote:s=>s.note});c.paint();
const roll=host.children[1],notes=roll.children[2],event=(id,x,y,target=roll,type='pointermove')=>({pointerId:id,clientX:x,clientY:y,target,type,pointerType:'touch',preventDefault(){}});
// A second finger switches a tentative background tap to navigation without adding notes.
roll.onpointerdown(event(1,300,100));roll.onpointerdown(event(2,600,200));roll.onpointermove(event(1,200,100));roll.onpointerup(event(2,600,200,roll,'pointerup'));roll.onpointerup(event(1,200,100,roll,'pointerup'));assert.equal(published.length,0);
view.start=0;view.span=16;view.low=55;view.rows=13;c.paint();
let hit=notes.children[0];const before=JSON.stringify(state);
roll.onpointerdown(event(1,10,150,hit));roll.onpointermove(event(1,110,130,hit));assert.equal(JSON.stringify(state),before);roll.onpointercancel(event(1,110,130,hit,'pointercancel'));assert.equal(JSON.stringify(state),before);
// Resize changes only the chosen note duration; the chord and legacy gate stay intact.
hit=notes.children[0];roll.onpointerdown(event(1,24,150,hit));roll.onpointermove(event(1,49,150,hit));roll.onpointerup(event(1,49,150,hit,'pointerup'));assert.equal(state.sequence[0][0].gate,.5);assert.equal(state.sequence[0][0].durations[60],1);assert.equal(state.sequence[0][0].durations[64],undefined);assert.deepEqual(state.sequence[0][0].notes,[60,64]);assert.deepEqual(published,[0]);
published.length=0;hit=notes.children[0];roll.onpointerdown(event(1,10,150,hit));roll.onpointermove(event(1,60,130,hit));roll.onpointerdown(event(2,400,200));roll.onpointerup(event(2,400,200,roll,'pointerup'));roll.onpointerup(event(1,60,130,roll,'pointerup'));assert.equal(published.length,0);assert.deepEqual(state.sequence[0][0].notes,[60,64]);
c.cancel();assert.equal(roll.captured.size,0);
console.log('Piano roll: anchored two-axis zoom, bounded pan, one-step lanes, navigation without edits, staged/cancelled moves, independent duration and teardown passed.');

// Two stationary taps delete only that pitch; undo restores the chord and its per-note duration.
c.cancel();view.start=0;view.span=16;view.low=55;view.rows=13;c.paint();
const originalStep=JSON.stringify(state.sequence[0][0]);
function noteTap(time){const hit=notes.children.find(n=>n.dataset.step===0&&n.dataset.note===60),up=event(9,10,150,hit,'pointerup');up.timeStamp=time;roll.onpointerdown(event(9,10,150,hit));roll.onpointerup(up);}
noteTap(1000);assert.deepEqual(state.sequence[0][0].notes,[60,64]);noteTap(1100);assert.deepEqual(state.sequence[0][0].notes,[64]);
const undo=tools.children.find(n=>n.cls==='roll-undo');assert.equal(undo.disabled,false);undo.onclick();assert.equal(JSON.stringify(state.sequence[0][0]),originalStep);assert.equal(tools.children.find(n=>n.cls==='roll-redo').disabled,false);
noteTap(2000);c.cancel();noteTap(2100);assert.deepEqual(state.sequence[0][0].notes,[60,64],'pause/cancel prevents a false double tap');
c.cancel();c.deleteNote(0,60);model.edit(0,{velocity:20});c.syncUndo();assert.equal(undo.disabled,true,'do not overwrite a newer edit with an old deletion snapshot');
console.log('Piano roll deletion: direct double tap, chord/duration-safe undo, pause separation and stale-undo protection passed.');

// Multiple selection deletes as one action, then Undo/Redo restores exact independent durations.
c.cancel();const select=tools.children.find(n=>n.cls==='roll-edit-actions').children.find(n=>n.cls==='roll-select'),erase=tools.children.find(n=>n.cls==='roll-edit-actions').children.find(n=>n.cls==='roll-erase'),redo=tools.children.find(n=>n.cls==='roll-redo');
select.onclick();for(const n of [64]){const hit=notes.children.find(b=>b.dataset.note===n);roll.onpointerdown(event(11,10,150,hit));roll.onpointerup(event(11,10,150,hit,'pointerup'));}assert.equal(erase.disabled,false);erase.onclick();assert.equal(state.sequence[0][0].on,false);undo.onclick();assert.deepEqual(state.sequence[0][0].notes,[64]);redo.onclick();assert.equal(state.sequence[0][0].on,false);undo.onclick();

// Region selection includes both chord tones and is a single atomic history command.
model.write(model.steps[0],[60,64],{60:2,64:1});model.edit(0,{});c.paint();const regionBefore=JSON.stringify(model.steps[0]),r=roll.getBoundingClientRect();
roll.onpointerdown(event(20,r.left,r.top));roll.onpointermove(event(20,r.left+r.width,r.top+r.height));roll.onpointerup(event(20,r.left+r.width,r.top+r.height,roll,'pointerup'));assert.equal(erase.textContent,'APAGAR (2)');erase.onclick();assert.equal(model.steps[0].on,false);undo.onclick();assert.equal(JSON.stringify(model.steps[0]),regionBefore);redo.onclick();assert.equal(model.steps[0].on,false);
console.log('Piano roll region selection and grouped chord deletion Undo/Redo passed.');

// Moving a selected note follows the moved identity, so a subsequent group-delete targets its new position.
select.onclick();model.write(model.steps[0],[60],{60:2});model.edit(0,{});c.paint();hit=notes.children.find(n=>n.dataset.step===0&&n.dataset.note===60);roll.onpointerdown(event(25,10,150,hit));roll.onpointermove(event(25,60,150,hit));roll.onpointerup(event(25,60,150,hit,'pointerup'));assert(model.steps[1].on);assert.equal(erase.disabled,false);erase.onclick();assert.equal(model.steps[1].on,false);

// Desktop shortcuts stay within the roll and use the same transactions as touch.
const shortcut=(key,ctrlKey=false,shiftKey=false,target=roll)=>{let prevented=false;host.onkeydown({key,ctrlKey,shiftKey,target,preventDefault(){prevented=true;},stopPropagation(){}});return prevented;};
model.write(model.steps[0],[60,64],{60:2,64:1});model.edit(0,{});c.paint();shortcut('a',true);assert.equal(erase.textContent,'APAGAR (2)');
const groupBefore=JSON.stringify(state);hit=notes.children.find(n=>n.dataset.note===60);
roll.onpointerdown(event(30,24,150,hit));roll.onpointermove(event(30,74,150,hit));assert.equal(JSON.stringify(state),groupBefore);roll.onpointerup(event(30,74,150,hit,'pointerup'));
assert.deepEqual(model.steps[0].durations,{60:3,64:2});shortcut('z',true);assert.equal(JSON.stringify(state),groupBefore);shortcut('z',true,true);assert.deepEqual(model.steps[0].durations,{60:3,64:2});
shortcut('a',true);shortcut('ArrowRight');assert.deepEqual(model.steps[1].notes,[60,64]);assert(!model.steps[0].on);shortcut('c',true);model.select(4);shortcut('v',true);assert.deepEqual(model.steps[4].durations,{60:3,64:2});
assert(!shortcut('z',true,false,{tagName:'INPUT'}),'text editing keeps native shortcuts');shortcut('Escape');assert.equal(erase.disabled,true);
console.log('Roll shortcuts: selected-group edge resize preview/commit, exact Undo/Redo, arrows, copy/paste, Escape and text-field isolation passed.');
