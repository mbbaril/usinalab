// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {installPerformance} from '../performance.js';
class Element{
  constructor(){this.value=0;this.attrs={};this.styles={};this.style={setProperty:(k,v)=>this.styles[k]=v};this.classList={toggle:()=>{}};}
  before(host){this.host=host;}
  append(){}
  setAttribute(k,v){this.attrs[k]=v;}
  setPointerCapture(id){this.captured=id;}
  hasPointerCapture(id){return this.captured===id;}
  releasePointerCapture(id){if(this.captured===id)this.captured=null;}
  getBoundingClientRect(){return {bottom:110,height:100};}
}
const pitch=new Element(),mod=new Element(),range=new Element();Object.assign(pitch,{min:-8192,max:8191});Object.assign(mod,{min:0,max:127});
globalThis.document={getElementById:id=>({'pitch-wheel':pitch,'mod-wheel':mod,'bend-range':range}[id]),createElement:()=>new Element()};
const messages=[],performance=installPerformance({note:(...m)=>messages.push(m),getRange:()=>12,setRange:()=>{}}),event=(id,y,type='pointermove')=>({pointerId:id,clientY:y,type,preventDefault(){}});
performance.receiveMidi(0xef,127,127);assert.equal(+pitch.value,8191);assert.equal(messages.length,0);assert.equal(pitch.host.styles['--strip-position'],1);
performance.receiveMidi(0xb3,1,96);assert.equal(+mod.value,96);assert.equal(messages.length,0);
performance.receiveMidi(0xb0,74,127);assert.equal(+mod.value,96);
pitch.onpointerdown(event(4,10));assert.deepEqual(messages.at(-1),[0xe0,127,127]);const count=messages.length;
pitch.onpointermove(event(5,110));pitch.onpointerup(event(5,110));assert.equal(messages.length,count);assert.equal(+pitch.value,8191);
pitch.onpointermove(event(4,110));assert.deepEqual(messages.at(-1),[0xe0,0,0]);pitch.onpointercancel(event(4,100));assert.deepEqual(messages.at(-1),[0xe0,0,64]);assert.equal(+pitch.value,0);
mod.onpointerdown(event(8,10));mod.onpointerup(event(8,110,'pointerup'));assert.equal(+mod.value,0);assert.deepEqual(messages.at(-1),[0xb0,1,0]);assert.equal(mod.captured,null);
const before=messages.length;performance.receiveMidi(0xb9,121,0);assert.equal(+pitch.value,0);assert.equal(+mod.value,0);assert.equal(messages.length,before);
performance.reset();assert.deepEqual(messages.slice(-2),[[0xe0,0,64],[0xb0,1,0]]);performance.sync();assert.equal(+range.value,12);
pitch.onpointerdown(event(9,10));performance.reset();assert.equal(pitch.captured,null);performance.receiveMidi(0xe0,127,127);const countAfterReset=messages.length;pitch.onpointerup(event(9,110,'pointerup'));pitch.onpointermove(event(9,110));assert.equal(+pitch.value,8191);assert.equal(messages.length,countAfterReset,'late release does not override incoming MIDI bend after reset');
delete globalThis.document;
console.log('Performance strips: full-resolution bend, CC1 latch, pointer ownership, cancel centering and incoming MIDI readback without echo: pass');
