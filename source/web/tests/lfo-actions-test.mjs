// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {lfoShapes,mountLfoActions} from '../lfo-actions.js';
const native=readFileSync(new URL('../../src/plugin/ui/Panels.cpp',import.meta.url),'utf8');assert(native.includes('shapeWave[6] = { 4, 0, 3, 1, 2, 5 }'));assert.deepEqual(lfoShapes.map(s=>s[1]),[4,0,3,1,2,5]);
const el=(tag,cls='',text='')=>({cls,textContent:text,children:[],dataset:{},attrs:{},append(...v){this.children.push(...v);},setAttribute(k,v){this.attrs[k]=v;}});
for(let index=0;index<3;index++) {
  const p=`lfo${index+1}_`,values={[p+'wave']:4},dim={},taps=[];let time=100;
  const root=el('section');root.querySelector=selector=>({classList:{toggle:(key,value)=>dim[selector]=value}});
  const bar=mountLfoActions({root,el,index,param:id=>values[id]??0,setParam:(id,value)=>values[id]=value,tap:(i,t)=>taps.push([i,t]),readback:()=>taps.length?'2.00 Hz':'',clock:()=>time});
  for(let i=0;i<6;i++){bar.children[i].onclick();assert.equal(values[p+'wave'],lfoShapes[i][1]);bar.sync();assert.equal(bar.children[i].attrs['aria-pressed'],true);}
  assert.equal(dim[`[data-param="${p}speed"]`],false);assert.equal(dim[`[data-param="${p}fade"]`],true);assert.equal(dim[`[data-param="${p}wave_b"]`],true);
  values[p+'sync']=9;values[p+'key_sync']=1;values[p+'morph']=50;bar.sync();assert.equal(dim[`[data-param="${p}speed"]`],true);assert.equal(dim[`[data-param="${p}fade"]`],false);assert.equal(dim[`[data-param="${p}wave_b"]`],false);
  const tap=bar.children.at(-1);tap.onpointerdown({pointerType:'touch',isPrimary:true,preventDefault(){}});assert.deepEqual(taps,[[index,100]]);time=300;tap.onclick({detail:1});assert.equal(taps.length,1);tap.onclick({detail:0});assert.deepEqual(taps.at(-1),[index,300]);assert.equal(tap.textContent,'TAP 2.00 Hz');
}
console.log('LFO 1–3: six native waveform shortcuts, sync/key-sync/morph dim states and press-timed shared TAP passed.');

// WebView can synthesize a detail=0 click after touch: one physical press still means one TAP.
{const root=el('div'),v={lfo1_wave:0},t=[];root.querySelector=()=>null;mountLfoActions({root,el,index:0,param:id=>v[id]||0,setParam:(id,x)=>v[id]=x,tap:(i,x)=>t.push(x),readback:()=>'',clock:()=>100});const b=root.children.at(-1).children.at(-1);b.onpointerdown({pointerType:'touch',isPrimary:true,preventDefault(){}});b.onpointerup();b.onclick({detail:0});assert.equal(t.length,1);b.onkeydown();b.onclick({detail:0});assert.equal(t.length,2);}
