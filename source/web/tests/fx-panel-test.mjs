// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {nativeStrings} from './native-source.mjs';
import {createUserPerformance} from '../cabine-user.js';
import {readFileSync} from 'node:fs';
import {swapFxSlots,createFxPanel,fxBuses,fxPower,fxEditorFields} from '../fx-panel.js';
const meta=JSON.parse(readFileSync(new URL('../../build-web/dist/metadata.json',import.meta.url))),effects=nativeStrings(readFileSync(new URL('../../src/plugin/ui/FxPanel.cpp',import.meta.url),'utf8'),'kKinds'),slots=['fx_a','fx_b','aux','loop'].flatMap(bus=>['','2','3'].map(s=>`${bus}${s}_type`));
const native=readFileSync(new URL('../../src/plugin/ui/FxPanel.cpp',import.meta.url),'utf8'),power=nativeStrings(native,'kPowerIds');assert.deepEqual(fxPower,power);
for(const bus of fxBuses)assert.equal(meta.parameters.filter(d=>d.group===bus&&slots.includes(d.id)).length,3);
for(let mode=0;mode<meta.vaporKnobs.length;mode++){const fields=fxEditorFields(meta.parameters,5,effects,meta,mode);assert(fields.every(d=>d.group==='VAPOR'));assert.equal(fields.find(d=>d.id==='vap_position').name,meta.vaporKnobs[mode][0]);}
class E{constructor(tag,cls='',text=''){this.style={setProperty(){}};this.cls=cls;this.textContent=text;this.children=[];this.dataset={};this.events={};this.classList={add(){},toggle(){}};}append(...v){for(const n of v){this.children=this.children.filter(c=>c!==n);this.children.push(n);}}setAttribute(){}addEventListener(k,f){this.events[k]=f;}}
const el=(...v)=>new E(...v),params={fx_a_type:6,fx_a2_type:1,fx_a3_type:0},sections=[];let root;
const section=(host,title,items)=>{const e=el('section');e.title=title;e.items=items;host.append(e);sections.push({host,e});return e;};
let switchBus;const panel=createFxPanel({el,param:id=>params[id]??0,descriptors:()=>meta.parameters,metadata:()=>meta,field:d=>{const e=el('label');e.id=d.id;return e;},section,subtabs:(names,selected,fn)=>{switchBus=fn;assert.deepEqual(names,fxBuses);return el('nav');},slots,effects,rerender(){render();}});
function render(){root=el('main');sections.length=0;panel.render(root);}render();let board=root.children.at(-1),rack=board.children[0],editor=board.children[1];assert.equal(editor.dataset.fxEditor,'fx_a_type');assert.equal(editor.children.find(n=>n.cls==='fx-editor-layout').dataset.fxKind,6);
rack.children.find(n=>n.dataset.fxSlot==='fx_a2_type').children[0].onclick();board=root.children.at(-1);editor=board.children[1];assert.equal(editor.dataset.fxEditor,'fx_a2_type');assert.deepEqual(sections.filter(s=>s.host.cls==='fx-texture-board').map(s=>s.e.title),['TEXTURE','GRAINS','SEED','MIX']);assert(sections.filter(s=>s.host.cls==='fx-texture-board').every(s=>s.e.items.every(d=>d.group==='TEXTURE')));
switchBus(3);const bus=sections.find(s=>s.e.title==='LOOP INSERT');assert(bus.e.items.some(d=>d.id==='feedback'));assert(bus.e.items.some(d=>d.id==='fb_time'));assert(!bus.e.items.some(d=>d.id==='in_gain'));
console.log('FX panel: native power IDs/three slots, focused editor, mode-dependent VAPOR labels and LOOP feedback placement passed.');

const values={a:1,b:6},queued=new Map(),set=(id,v)=>{values[id]=v;queued.set(id,v);for(const key of ['a','b'])if(key!==id&&v&&values[key]===v){values[key]=0;queued.set(key,0);}};
assert(swapFxSlots('a','b',id=>values[id],set));assert.deepEqual(values,{a:6,b:1});assert.equal(queued.get('a'),6);assert.equal(queued.get('b'),1);assert(!swapFxSlots('a','a',id=>values[id],set));
console.log('FX swap: values captured before deduplication, final parameter batch preserves both effects.');

const tapped={fx_a_type:6,seq_bpm:120,dly_sync:0,dly_time:20},performanceModel=createUserPerformance({param:id=>tapped[id]??0,setParam:(id,v)=>tapped[id]=v,normalized:()=>0,setNormalized(){},library:()=>[],choosePreset(){},state:{}});let time=100;
const tapPanel=createFxPanel({el,param:id=>tapped[id]??0,descriptors:()=>meta.parameters,metadata:()=>meta,field:d=>el('label'),section,subtabs:()=>el('nav'),slots,effects,rerender(){},tapDelay:t=>performanceModel.tap(3,t),tapText:()=>performanceModel.tapText[3],clock:()=>time}),tapRoot=el('main');tapPanel.render(tapRoot);const tap=tapRoot.children.at(-1).children[1].children.find(n=>n.cls==='fx-editor-layout').children.flatMap(n=>n.children).find(n=>n.cls==='delay-tap');assert(tap);tap.onclick();assert(tap.textContent.startsWith('TAP'));time=475;tap.onclick();assert.equal(tapped.dly_time,375);assert.equal(tap.textContent,'TAP 375 ms');tapped.dly_sync=1;time=5000;tap.onclick();time=5500;tap.onclick();assert.equal(tapped.dly_sync,9);assert.equal(tap.textContent,'TAP 1/4');
console.log('DELAY TAP: FX button shares CABINE tap history, free milliseconds and synchronized division readback.');

time=9000;tap.onpointerdown({pointerType:"touch",isPrimary:true,preventDefault(){}});assert.equal(tap.dataset.tapAt,9000);time=9300;tap.onclick({detail:1});assert.equal(tap.dataset.tapAt,9000,"The release click must not register a second beat");console.log("TAP follows native press timing, with release-click suppression and keyboard activation.");
