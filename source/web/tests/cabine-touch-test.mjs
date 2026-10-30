// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {boundControl,bindPerformanceControl,createRouteInspector,renderGestureStrips} from '../cabine-touch.js';
function el(tag,cls='',text=''){
 const attributes={},captures=new Set(),children=[];
 return{tag,className:cls,textContent:text,dataset:{},style:{setProperty(k,v){this[k]=v;}},children,append(...items){children.push(...items);},setAttribute(k,v){attributes[k]=String(v);},getAttribute:k=>attributes[k],setPointerCapture:id=>captures.add(id),hasPointerCapture:id=>captures.has(id),releasePointerCapture:id=>captures.delete(id),querySelectorAll(){const found=[];const visit=n=>{if(n.dataset.gestureMacro!==undefined)found.push(n);n.children.forEach(visit);};children.forEach(visit);return found;}};
}
const event=(type,pointerId,clientY=100,extra={})=>({type,pointerId,clientY,button:0,preventDefault(){},...extra});
assert.equal(boundControl(NaN),null);assert.equal(boundControl(2),1);assert.equal(boundControl(-2,-1,1),-1);
let amount=.25;const target=el('div'),cancel=bindPerformanceControl({target,get:()=>amount,set:v=>amount=v,min:-1,max:1,sensitivity:150});
target.onpointerdown(event('pointerdown',1));target.onpointerdown(event('pointerdown',2));target.onpointermove(event('pointermove',2,0));assert.equal(amount,.25,'second finger cannot steal a control');
target.onpointerup(event('pointerup',1,25));assert.equal(amount,.75,'final position is applied');
target.onpointerdown(event('pointerdown',1));target.onpointermove(event('pointermove',1,1000));assert.equal(amount,-1);cancel();target.onpointerup(event('pointerup',1,0));assert.equal(amount,-1,'cancellation rejects late release');
target.onkeydown(event('keydown',0,0,{key:'End'}));assert.equal(amount,1);target.onkeydown(event('keydown',0,0,{key:'ArrowDown',shiftKey:true}));assert.equal(amount,.998);
let route={amount:-.5,name:'LFO 1 → CUTOFF',color:'#25e0c4'};const inspector=createRouteInspector({el,selected:()=>route,update:v=>route.amount=v});
const controls=inspector.root.children[2],dial=controls.children[0];assert.equal(dial.getAttribute('aria-valuenow'),'-0.5');controls.children[2].onclick();assert.equal(route.amount,0);
route=null;inspector.refresh();assert.equal(controls.hidden,true,'deleted routes cannot remain editable');
const values={a:.2,b:.7,c:.1},root=el('section'),surface=renderGestureStrips({root,el,groups:[['a'],['b','c']],names:['MACRO','PAIR'],normalized:id=>values[id],setNormalized:(id,v)=>values[id]=v,readout:ids=>ids.map(id=>values[id]).join('/'),pulse(){}});
const strips=root.children[0].children[0].children.map(field=>field.children[1]);strips[0].onpointerdown(event('pointerdown',11));strips[1].onpointerdown(event('pointerdown',12));strips[0].onpointermove(event('pointermove',11,78));assert(Math.abs(values.a-.3)<1e-9);assert.equal(values.b,.7);
strips[1].onpointerup(event('pointerup',12,78));assert(Math.abs(values.b-.8)<1e-9);assert.equal(values.c,values.b,'paired bus gesture writes both existing parameters');surface.cancel();strips[0].onpointerup(event('pointerup',11,0));assert(Math.abs(values.a-.3)<1e-9);
console.log('CABINE touch: bipolar bounds, pointer ownership, final coordinates, cancellation, keyboard, deleted route and independent/paired gesture controls passed.');
