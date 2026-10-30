// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {installPerformanceKeys,installReleaseGuard} from '../performance-keyboard.js';
import {createPlaySurface} from '../play-surface.js';
import {createCabineRemote,remote} from '../cabine-remote.js';
import {MidiOutput} from '../midi-output.js';
import {el,Node,environment} from './performance-dom.mjs';
let {win,doc}=environment(),events=[];
const items=[0,1,2,3].map(i=>({target:el('div'),row:Math.floor(i/2),col:i%2,press:()=>events.push(['on',i]),release:()=>events.push(['off',i])}));
const keys=installPerformanceKeys(items),unguard=installReleaseGuard(keys.release,win,doc),a=items[0].target;
a.focus();a.fire('keydown',{key:' ',repeat:false});a.fire('keydown',{key:' ',repeat:true});assert.deepEqual(events,[['on',0]]);
a.fire('keydown',{key:'ArrowRight'});assert.equal(Node.active,items[1].target);assert.deepEqual(events.at(-1),['off',0]);assert.equal(items.filter(i=>i.target.tabIndex===0).length,1);
Node.active.fire('keydown',{key:'ArrowDown'});assert.equal(Node.active,items[3].target);Node.active.fire('keydown',{key:'Enter'});win.fire('blur');assert.deepEqual(events.at(-1),['off',3]);
Node.active.fire('keydown',{key:'Enter'});doc.hidden=true;doc.fire('visibilitychange');assert.deepEqual(events.at(-1),['off',3]);keys.dispose();unguard();

// Exercise the rendered surface through its event handlers and MIDI callback.
for(const slide of ['legato','bend',null]){
  ({win,doc}=environment({slide,manuals:1,panel:'full'}));const midi=[],macros=[0,0,0,0];let mpe=false;
  const play=createPlaySurface({el,status(){},local:{note:(...m)=>midi.push(m),macro:i=>macros[i],setMacro:(i,v)=>macros[i]=v,mpe:()=>mpe,setMpe:v=>mpe=v}}),root=el('main');play.render(root);
  let key=root.querySelector('.play-key');key.focus();key.fire('keydown',{key:'Enter'});key.fire('keydown',{key:'Enter',repeat:true});
  const ons=()=>midi.filter(m=>(m[0]&240)===144&&m[2]>0),offs=()=>midi.filter(m=>(m[0]&240)===128);assert.equal(ons().length,1);
  key.fire('keyup',{key:'Enter'});assert.equal(offs().length,1);assert.equal(ons()[0][1],offs()[0][1]);assert.equal(ons()[0][0]&15,offs()[0][0]&15);
  key.fire('keydown',{key:' '});key.fire('blur');assert.equal(offs().length,2);
  key.fire('keydown',{key:' '});win.fire('blur');assert.equal(offs().length,3);
  key.fire('keydown',{key:' '});doc.hidden=true;doc.fire('visibilitychange');assert.equal(offs().length,4);doc.hidden=false;doc.fire('visibilitychange');
  key.fire('keydown',{key:' '});root.querySelector('.play-oct').querySelector('button').onclick();assert.equal(offs().length,5,'octave rerender releases old pitch');
  key=root.querySelector('.play-key');key.fire('keydown',{key:'Enter'});play.leave();assert.equal(offs().length,6,'leaving releases notes');
  assert.equal(ons().length,offs().length);
  const balance=new Map();for(const [status,note,velocity]of midi){const kind=status&240;if(kind!==128&&kind!==144)continue;const id=`${status&15}:${note}`,delta=kind===144&&velocity>0?1:-1;balance.set(id,(balance.get(id)||0)+delta);assert(balance.get(id)>=0,'release must match a started channel/pitch');}assert([...balance.values()].every(n=>n===0),'every channel/pitch must finish released');
}
({win,doc}=environment({slide:null,manuals:1,panel:'full'}));const resizeMidi=[];
const resizePlay=createPlaySurface({el,status(){},local:{note:(...m)=>resizeMidi.push(m),macro:()=>0,setMacro(){},mpe:()=>true}}),resizeRoot=el('main');resizePlay.render(resizeRoot);
resizeRoot.querySelector('.play-key').fire('keydown',{key:'Enter'});resizeRoot.clientWidth=1280;resizePlay.resize();
const resizeNotes=resizeMidi.filter(m=>[128,144].includes(m[0]&240));assert.equal(resizeNotes.length,2);assert.equal(resizeNotes[0][0]&15,1);assert.equal(resizeNotes[1][0]&15,1,'resize must release through the encoder that started the note');resizePlay.leave();
({win,doc}=environment({slide:'legato',panel:'full',snaps:[[1,1,1,1],[-1,-1,-1,-1],[1,1,1,1],[-1,-1,-1,-1]],set:15}));
const macros=[0,0,0,0],play=createPlaySurface({el,status(){},local:{note(){},macro:i=>macros[i],setMacro:(i,v)=>macros[i]=v}}),root=el('main');play.render(root);
const [x,y]=root.querySelectorAll('.play-xy-axis');x.onkeydown({key:'End',preventDefault(){}});assert.equal(x.getAttribute('aria-valuenow'),'1');assert(macros.every(v=>v===-1));y.onkeydown({key:'Home',preventDefault(){}});assert.equal(y.getAttribute('aria-valuenow'),'0');play.leave();

({win,doc}=environment());const packets=[];
MidiOutput.prototype.connect=async function(){this.destinations=[{key:'test',name:'Test'}];this.onChange();};
MidiOutput.prototype.select=async function(){this.selected=this.destinations[0];this.ready=true;this.onChange();};
MidiOutput.prototype.send=async function(messages){packets.push(...messages);};
for(const mpe of [false,true]){
  ({win,doc}=environment({dest:'control',mpe,panel:'full'}));const outPlay=createPlaySurface({el,status(){},local:{note(){throw Error('Unexpected local note');},macro:()=>0,setMacro(){}}}),outRoot=el('main');outPlay.render(outRoot);
  await outRoot.querySelectorAll('button').find(b=>b.textContent==='Buscar saídas').onclick();const port=outRoot.querySelector('select');port.value='test';port.onchange();await new Promise(resolve=>setImmediate(resolve));packets.length=0;
  const note=outRoot.querySelector('.play-key');note.fire('keydown',{key:'Enter'});note.fire('keydown',{key:'Enter',repeat:true});doc.hidden=true;doc.fire('visibilitychange');
  const notes=packets.filter(m=>[128,144].includes(m[0]&240));assert.equal(notes.length,2);assert.equal(notes[0][0]&15,mpe?1:0);assert.equal(notes[0][0]&15,notes[1][0]&15);assert.equal(notes[0][1],notes[1][1]);assert.equal(notes[1][0]&240,128);outPlay.leave();
}
({win,doc}=environment());packets.length=0;
const surface=createCabineRemote({el,status(){},listen(){}}),remoteRoot=el('main');surface.render(remoteRoot);await remoteRoot.querySelectorAll('button').find(b=>b.textContent==='Buscar portas').onclick();const select=remoteRoot.querySelector('select');select.value='test';select.onchange();await Promise.resolve();await Promise.resolve();packets.length=0;
const pad=remoteRoot.querySelector('.remote-pad');pad.focus();pad.fire('keydown',{key:' '});pad.fire('keydown',{key:' ',repeat:true});assert.deepEqual(packets,[remote.pad(0,0,true)]);
pad.onpointerdown({pointerId:42,currentTarget:pad,preventDefault(){}});pad.fire('keyup',{key:' '});assert.equal(packets.length,1,'pointer still owns pad');pad.onpointerup({pointerId:42});assert.deepEqual(packets.at(-1),remote.pad(0,0,false));
pad.fire('keydown',{key:'Enter'});win.fire('blur');assert.deepEqual(packets.at(-1),remote.pad(0,0,false));pad.fire('keydown',{key:'Enter'});doc.hidden=true;doc.fire('visibilitychange');assert.deepEqual(packets.at(-1),remote.pad(0,0,false));doc.hidden=false;
const before=packets.length;pad.fire('keydown',{key:'Enter',repeat:true});assert.equal(packets.length,before,'repeat cannot rearm after visibility release');
pad.fire('keydown',{key:'Enter'});surface.leave();assert.deepEqual(packets.at(-1),remote.pad(0,0,false));assert.equal(packets.filter(m=>(m[0]&240)===144).length,packets.filter(m=>(m[0]&240)===128).length);
console.log('Performance keyboard: roving focus, repeat suppression, balanced local/MPE notes, octave/blur/leave release, XY macro interpolation and remote pointer/key ownership passed.');
