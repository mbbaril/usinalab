// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {cabinePitch,cabineScales,cabineLayout,cabineNoteName,installCabineKeys} from '../cabine-keys.js';
assert.equal(cabinePitch(7,0),48);assert.equal(cabinePitch(7,2),51);assert.equal(cabinePitch(6,0),53);assert.equal(cabineNoteName(60),'C3');
assert.equal(cabinePitch(6,0,{scale:6,root:0,octave:4}),53);assert.equal(cabinePitch(6,0,{scale:7,root:0,octave:4}),53);
for(let scale=0;scale<8;++scale)for(let root=0;root<12;++root)for(let octave=0;octave<10;++octave)for(let row=0;row<8;++row)for(let col=0;col<8;++col){const n=cabinePitch(row,col,{scale,root,octave});assert(n===-1||n>=0&&n<=127);if(row===7&&col===0)assert.equal(n,octave*12+root<128?octave*12+root:-1);}
assert.equal(cabineScales.length,8);assert.equal(cabineLayout(null).octave,4);assert.equal(cabineLayout({octave:30,scale:-4}).octave,9);assert.equal(cabineLayout({layer:'unknown'}).layer,'SESSION');
// Two physical pads share an isomorphic pitch. Releasing one must keep the other sounding.
const pads=Array.from({length:64},()=>({classList:{add(){},toggle(){}},style:{setProperty(){}},dataset:{},setAttribute(){},setPointerCapture(){}})),sent=[];
const stop=installCabineKeys({pads,config:cabineLayout(),note:(...m)=>sent.push(m),pulse(){}}),down=id=>({pointerId:id,pointerType:'touch',preventDefault(){}});
pads[48].onpointerdown(down(1));pads[59].onpointerdown(down(2));assert.deepEqual(sent,[[144,53,100]]);
pads[48].onpointerup(down(1));assert.equal(sent.length,1);pads[59].onpointercancel(down(2));assert.deepEqual(sent.at(-1),[128,53,0]);
pads[56].onpointerdown(down(3));stop();assert.deepEqual(sent.at(-1),[128,48,0]);stop();assert.equal(sent.length,4);
console.log('CABINE KEYS: all eight native scales, 61,440 layouts/pitches, octave bounds, duplicate-pad ownership and cancellation passed.');
