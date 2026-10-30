// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {installFxSlotDrag} from '../fx-slot-drag.js';
let capture=null, starts=0, ends=0, selects=0, target='aux3_type', drops=[];
const handle={setPointerCapture:id=>capture=id,hasPointerCapture:id=>capture===id,releasePointerCapture:()=>capture=null},row={dataset:{}};
const cancel=installFxSlotDrag({handle,row,onSelect:()=>selects++,onStart:()=>starts++,onEnd:()=>ends++,onMove:()=>target,onDrop:id=>drops.push(id)});
const e=(type,x=0,id=1)=>({type,clientX:x,clientY:0,pointerId:id,pointerType:'touch',preventDefault(){}});
handle.onpointerdown(e('pointerdown'));handle.onpointerup(e('pointerup',2));handle.onclick();assert.equal(selects,1);assert.equal(starts,0);
// Coalesced final movement still commits the slot under the final release.
handle.onpointerdown(e('pointerdown'));handle.onpointerup(e('pointerup',100));assert.deepEqual(drops,['aux3_type']);assert.equal(capture,null);assert.equal(row.dataset.dragging,undefined);handle.onclick();assert.equal(selects,1);
handle.onpointerdown(e('pointerdown'));handle.onpointermove(e('pointermove',20));cancel();handle.onpointerup(e('pointerup',100));assert.equal(drops.length,1);
// An old capture loss must not cancel a new contact reusing the same ID.
handle.onpointerdown(e('pointerdown'));handle.onlostpointercapture(e('lostpointercapture'));assert.equal(capture,1);handle.onpointerdown(e('pointerdown',200,2));target=null;handle.onpointerup(e('pointerup',100));assert.equal(drops.length,1);assert.equal(capture,null);assert.equal(starts,3);assert.equal(ends,4);
handle.onpointerdown(e('pointerdown'));handle.onpointerup(e('pointerup',1));handle.onclick();assert.equal(selects,2,'A fresh tap works immediately after an outside drop');
console.log('FX slot drag: final-position drop, tap focus, cancel, outside drop and reused-pointer ownership passed.');
