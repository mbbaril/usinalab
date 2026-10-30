// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {matrixTransform,installMatrixViewport} from '../matrix-viewport.js';
const initial={scale:1,distance:200,anchor:{x:400,y:200}};
assert.deepEqual(matrixTransform(initial,{x:200,y:200},{x:600,y:200},800,400),{scale:2,x:-400,y:-200});
assert.deepEqual(matrixTransform(initial,{x:390,y:200},{x:410,y:200},800,400),{scale:1,x:0,y:0});
const listeners={},captured=new Set(),frame={dataset:{},addEventListener:(n,f)=>listeners[n]=f,getBoundingClientRect:()=>({left:0,top:0,width:800,height:400}),setPointerCapture:id=>captured.add(id),hasPointerCapture:id=>captured.has(id),releasePointerCapture:id=>captured.delete(id)},grid={style:{}},headers={style:{}};
const view=installMatrixViewport({frame,grid,headers}),event=(id,x,pad=0)=>({pointerId:id,clientX:x,clientY:200,pointerType:'touch',detail:1,target:{closest:()=>({dataset:{pad}})},preventDefault(){this.prevented=true;},stopPropagation(){this.stopped=true;}});
listeners.pointerdown(event(1,300));listeners.pointerdown(event(2,500));listeners.pointermove(event(2,700));assert.equal(frame.dataset.zoom,'2.000');
listeners.pointerup(event(2,700));listeners.pointerup(event(1,300));const click=event(1,300);listeners.click(click);assert(click.prevented&&click.stopped);
listeners.pointerdown(event(1,300));listeners.pointerup(event(1,300));const validClick=event(1,300);listeners.click(validClick);assert(!validClick.prevented,'A new tap is not swallowed after navigation');
view.reset();assert.equal(frame.dataset.zoom,'1.000');assert.equal(captured.size,0);
listeners.pointerdown(event(7,200,56));listeners.pointerdown(event(1,300));listeners.pointerdown(event(2,500));listeners.pointermove(event(2,700));assert.equal(frame.dataset.zoom,'1.000','A held macro excludes navigation');listeners.pointerup(event(7,200,56));view.cancel();
console.log('Matrix viewport: anchored zoom, bounds, consumed gesture clicks, immediate next tap, macro ownership and reset/cancel passed.');
