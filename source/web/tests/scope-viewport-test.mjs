// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {ScopeViewport,installScopeViewport} from '../scope-viewport.js';
const v=new ScopeViewport();v.navigate({seconds:5,gain:1,anchor:2.5,dx:.3,dy:.3},{x:.2,y:.2},{x:.8,y:.8});assert(Math.abs(v.seconds-2.5)<1e-9);assert(Math.abs(v.gain-2)<1e-9);assert(Math.abs(v.age-1.25)<1e-9);
v.constrain(.5);assert.equal(v.age,0,'Insufficient history never leaves the visible window entirely empty');v.seconds=.001;v.gain=100;v.age=100;v.constrain(5);assert.equal(v.seconds,.25);assert.equal(v.gain,8);assert.equal(v.age,4.75);
const captured=new Set(),svg={style:{},getBoundingClientRect:()=>({left:0,top:0,width:600,height:150}),setPointerCapture:id=>captured.add(id),hasPointerCapture:id=>captured.has(id),releasePointerCapture:id=>captured.delete(id)};let painted=0;v.reset();const cancel=installScopeViewport({svg,view:v,paint:()=>painted++}),e=(id,x,y)=>({pointerId:id,clientX:x,clientY:y,pointerType:'touch',preventDefault(){}});
svg.onpointerdown(e(1,200,50));svg.onpointermove(e(1,100,50));assert.equal(painted,0,'One finger does not change the display');svg.onpointerdown(e(2,400,100));svg.onpointermove(e(2,500,130));assert.equal(painted,1);assert(v.seconds<5);cancel();assert.equal(captured.size,0);v.reset();assert.deepEqual([v.seconds,v.gain,v.age],[5,1,0]);
console.log('Scope viewport: anchored time/gain zoom, history pan bounds, short history, two-finger ownership, reset and cancellation passed.');
