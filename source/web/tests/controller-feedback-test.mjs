// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {createModulationFeedback} from '../modulation-feedback.js';
const frames=[];globalThis.requestAnimationFrame=f=>frames.push(f);globalThis.innerWidth=1280;globalThis.innerHeight=720;globalThis.IntersectionObserver=class{observe(){}disconnect(){}};
globalThis.window={addEventListener(){}};globalThis.document={addEventListener(){},createElement:()=>({className:'',style:{}})};
const root=()=>({isConnected:true,dataset:{},style:{setProperty(){}},classList:{toggle(){}},attrs:{},append(){},setAttribute(k,v){this.attrs[k]=v;},removeAttribute(k){delete this.attrs[k];},getBoundingClientRect(){return {width:100,height:10,top:1,bottom:11,left:1,right:101};}});
const messages=[],state={routes:[]};let ready=true;const feedback=createModulationFeedback({metadata:()=>({sources:[]}),state,param:()=>0,send:m=>messages.push(m),ready:()=>ready});
const nodes=Array.from({length:8},root);nodes.forEach((n,i)=>feedback.mountSource(n,{index:40+i,id:`ctrl${i+1}`,name:`CTRL ${i+1}`}));
while(frames.length)frames.shift()();let sub=messages.findLast(m=>m.type==='modSubscribe');assert.deepEqual(sub.sources,[40,41,42,43,44,45,46,47]);
assert(nodes.every(n=>n.dataset.live===''),'no reading invented before an audio packet');
feedback.receive({token:sub.token-1,count:1,values:[40,.75]});assert.equal(nodes[0].dataset.live,'','stale subscription rejected');
feedback.receive({token:sub.token,count:4,values:[40,.5,41,2,42,-1,43,NaN]});assert.equal(nodes[0].attrs['aria-valuenow'],.5);assert.equal(nodes[1].dataset.live,1);assert.equal(nodes[2].dataset.live,0);assert.equal(nodes[3].attrs['aria-valuenow'],undefined);assert.equal(messages.at(-1).type,'modAck');
feedback.suspend(true);assert(nodes.every(n=>n.dataset.live===''),'pause clears all visible meters');assert.deepEqual(messages.at(-1).sources,[]);
feedback.suspend(false);sub=messages.at(-1);feedback.receive({token:sub.token,count:1,values:[40,.8]});assert.equal(nodes[0].dataset.live,Math.fround(.8));
ready=false;feedback.suspend(true);assert.equal(nodes[0].attrs['aria-valuenow'],undefined);feedback.clear();assert.deepEqual(messages.at(-1).sources,[]);
console.log('Eight controller meters: shared bounded subscription, no invented values, stale packet rejection, range clamp, pause/restart and ACK passed.');

const {mountControllerPanel}=await import('../controller-panel.js');
const element=(tag,cls='',text='')=>({tag,cls,textContent:text,children:[],dataset:{},attrs:{},style:{setProperty(){}},classList:{toggle(){}},append(...nodes){this.children.push(...nodes);},replaceChildren(...nodes){this.children=nodes;},addEventListener(){},setAttribute(k,v){this.attrs[k]=String(v);},value:''});
const panel=element('section'),bindings={controllerBindings:Array(8).fill(null)},drafts=Array.from({length:8},()=>({kind:'cc',channel:-1}));let learning=-1,persisted=0;
mountControllerPanel({root:panel,el:element,state:bindings,metadata:()=>({sources:[]}),descriptor:id=>({id}),field:d=>element('select','',d.name),persist:()=>persisted++,learn:i=>learning=learning===i?-1:i,forget:i=>bindings.controllerBindings[i]=null,learning:()=>learning,feedback:{mountSource(){}},drafts,rerender:()=>assert.fail('controller edits must not rebuild the instrument')});
assert.deepEqual(panel.children[0].children.map(n=>n.textContent),['MPE INPUT','MASTER BEND','NOTE BEND']);
const row=panel.children[1].children[1],type=row.children[1],number=row.children[2],channel=row.children[3],learn=row.children[4],forget=row.children[5];
learn.onclick();assert.equal(learn.attrs['aria-pressed'],'true');learn.onclick();assert.equal(learn.attrs['aria-pressed'],'false');
bindings.controllerBindings[0]={kind:'note',number:69,channel:2};row.sync();assert.equal(number.children[70].textContent,'A4 (69)');assert.equal(type.value,'note');assert.equal(number.value,69);assert.equal(channel.value,2);
number.value='72';channel.value='-1';number.onchange();assert.deepEqual(bindings.controllerBindings[0],{kind:'note',number:72,channel:-1});
type.value='cc';type.onchange();assert.equal(number.children[73].textContent,'CC 72');assert.equal(bindings.controllerBindings[0].number,72);assert.equal(persisted,2);
forget.onclick();assert.equal(number.value,-1);assert.equal(type.value,'cc');assert.equal(panel.children[1].children[1],row,'bindings update in place');
console.log('Controller rows retain identity through learn, type changes and Forget; native MPE captions and note labels passed.');
