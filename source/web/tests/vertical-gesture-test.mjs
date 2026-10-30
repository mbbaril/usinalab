// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {installVerticalGesture} from '../vertical-gesture.js';
for(const sensitivity of [150,220]){
  const captures=new Set(),target={dataset:{},setPointerCapture:id=>captures.add(id),hasPointerCapture:id=>captures.has(id),releasePointerCapture(id){captures.delete(id);this.onlostpointercapture(event('lostpointercapture',id,0));}};
  let value=.25;const cancel=installVerticalGesture({target,get:()=>value,set:v=>value=v,sensitivity});
  function event(type,id,y,button=0){return {type,pointerId:id,clientY:y,button,preventDefault(){},stopPropagation(){}};}
  target.onpointerdown(event('pointerdown',1,100));target.onpointermove(event('pointermove',2,0));assert.equal(value,.25);
  target.onlostpointercapture(event('lostpointercapture',1,100));target.onpointermove(event('pointermove',1,100-sensitivity*.5));assert.equal(value,.75,'stale lost capture does not end a captured drag');
  target.onpointerup(event('pointerup',1,100-sensitivity*.6));assert(Math.abs(value-.85)<1e-9,'final up coordinate applied');assert.equal(captures.size,0);
  target.onpointerdown(event('pointerdown',1,100));cancel();const before=value;target.onpointermove(event('pointermove',1,0));target.onpointerup(event('pointerup',1,0));assert.equal(value,before,'pause cancellation suppresses late events');
  target.onpointerdown(event('pointerdown',1,100));target.onpointermove(event('pointermove',1,100-sensitivity*.1));assert(Math.abs(value-before-.1)<1e-9,'same pointer ID can edit after resuming');cancel();
  target.onpointerdown(event('pointerdown',1,100,2));assert.equal(target.dataset.dragging,'false','context click does not begin editing');
}
console.log('Knob/mini gesture ownership, final coordinate, stale capture, pause cleanup and pointer reuse passed.');

for(const axis of ['x']){
 let value=0;const captures=new Set(),target={dataset:{},setPointerCapture:id=>captures.add(id),hasPointerCapture:id=>captures.has(id),releasePointerCapture:id=>captures.delete(id)};
 const cancel=installVerticalGesture({target,get:()=>value,set:v=>value=v,sensitivity:120,axis});
 const e=(type,id,x,shiftKey=false)=>({type,pointerId:id,clientX:x,clientY:100,shiftKey,button:0,preventDefault(){},stopPropagation(){}});
 target.onpointerdown(e('pointerdown',1,100));target.onpointermove(e('pointermove',2,220));assert.equal(value,0);
 target.onpointerup(e('pointerup',1,160));assert.equal(value,.5,'native RouteList deltaX/120, including final coordinate');
 target.onpointerdown(e('pointerdown',1,100));target.onpointermove(e('pointermove',1,160,true));assert.equal(value,.6,'fine gesture');cancel();target.onpointerup(e('pointerup',1,220));assert.equal(value,.6,'late up after cancellation ignored');
}
