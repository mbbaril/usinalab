// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {nativeFunction} from './native-source.mjs';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {fxLayouts,renderFxEditorLayout} from '../fx-editor-layout.js';
const native=readFileSync(new URL('../../src/plugin/ui/FxPanel.cpp',import.meta.url),'utf8'),meta=JSON.parse(readFileSync(new URL('../../build-web/dist/metadata.json',import.meta.url),'utf8'));
const header=readFileSync(new URL('../../src/plugin/ui/FxPanel.h',import.meta.url),'utf8'),nativeButtonLabels={},nativeInline=new Set();
const names={2:'Room',3:'Distort',5:'Vapor',6:'Delay',7:'FilterFx'},groups={2:'ROOM',3:'DISTORT',5:'VAPOR',6:'DELAY',7:'FILTER'};
for(const[kind,plan]of Object.entries(fxLayouts)){
  const ids=[...plan.controls.flat(),...(plan.body||[]),...plan.knobs.flat()].filter(id=>id!=='TAP');
  assert.equal(new Set(ids).size,ids.length,'No duplicated effect controls');
  assert.deepEqual([...ids].sort(),meta.parameters.filter(d=>d.group===groups[kind]).map(d=>d.id).sort(),`Every ${groups[kind]} parameter is exposed`);
  const constructor=nativeFunction(native,`${names[kind]}Panel::${names[kind]}Panel`),resized=nativeFunction(native,`void ${names[kind]}Panel::resized()`);
  const bindings=Object.fromEntries([...constructor.matchAll(/(\w+)\.bind\(st\.getParameter\("([^"]+)"\)/g)].map(m=>[m[1],m[2]]));
  for(const m of constructor.matchAll(/getParameter\("([^"]+)"\), (\w+)\)/g))bindings[m[2]]=m[1];bindings.tap='TAP';
  const panelHeader=header.split(`class ${names[kind]}Panel final`)[1].split('\n};')[0];for(const m of panelHeader.matchAll(/(\w+)\s*\{\s*"([^"]+)"\s*\}/g)){const id=bindings[m[1]];if(id&&id!=='TAP'){nativeButtonLabels[id]=m[2];nativeInline.add(id);}}
  for(const m of constructor.matchAll(/(\w+)\.setTitleVisible\(false\)/g))nativeInline.add(bindings[m[1]]);
  if(kind!=='2'){
    const rows=[[],[]];for(const m of resized.matchAll(/(\w+)\.setBounds\((row2?)\./g))rows[m[2]==='row2'?1:0].push(bindings[m[1]]);
    assert.deepEqual(plan.controls,rows.filter(r=>r.length),`${names[kind]} selector/switch order comes from native rows`);
  }
  if(kind==='3'||kind==='6'||kind==='7'){
    const rows=[...resized.matchAll(/for \(auto\* k : \{([^}]+)\}/g)].map(m=>[...m[1].matchAll(/&(\w+)/g)].map(v=>bindings[v[1]]));
    assert.deepEqual(plan.knobs,rows,`${names[kind]} knob order comes from native rows`);
  }
  if(kind==='5'){
    const idsBlock=constructor.match(/const char\* ids\[9\] = \{([^}]+)\}/)[1];assert.deepEqual(plan.knobs.flat(),[...idsBlock.matchAll(/"([^"]+)"/g)].map(m=>m[1]));assert.equal(plan.knobs[0].length,5);assert(resized.includes('line * 5'));
  }
  if(kind==='2'){assert(resized.indexOf('amount.setBounds')<resized.indexOf('color.setBounds'));}
}
const el=(tag,cls='',text='')=>({tag,cls,textContent:text,children:[],dataset:{},style:{setProperty(){}},append(...v){this.children.push(...v);}});
for(const kind of [2,3,5,6,7]){
  const root=el('main'),tap=el('button','','TAP'),fields=meta.parameters.filter(d=>d.group===groups[kind]).map(d=>d.id==='dist_char'?{...d,uiCaption:'MASK'}:d);
  assert(renderFxEditorLayout({root,el,kind,fields,field:d=>({...el('label'),descriptor:d}),tap}));
  const layout=root.children[0];assert.equal(layout.dataset.fxKind,kind);const nodes=layout.children.flatMap(n=>n.children);assert.equal(nodes.filter(n=>n===tap).length,kind===6?1:0);
  assert(nodes.filter(n=>n.descriptor).every(n=>n.descriptor.compactToggle));for(const node of nodes.filter(n=>n.descriptor)){assert.equal(node.descriptor.toggleLabel,nativeButtonLabels[node.descriptor.id]);assert.equal(node.dataset.fxCaption==='inline',nativeInline.has(node.descriptor.id));}const on=nodes.find(n=>n.descriptor?.id===({2:'rev_on',3:'dist_on',5:'vap_on',6:'dly_on',7:'fxf_on'})[kind]);assert.equal(on.descriptor.toggleLabel,groups[kind]);assert.equal(on.dataset.fxCaption,'inline');if(kind===3)assert.equal(nodes.find(n=>n.descriptor?.id==='dist_char').descriptor.uiCaption,'MASK');if(kind===2)assert.equal(nodes.find(n=>n.descriptor?.id==='rev_amount').descriptor.uiCaption,'AMOUNT');
}
console.log('FX layouts: all native effect parameters exactly once; native DELAY/DISTORT rows, VAPOR 5+4 order, ROOM body and dynamic captions passed.');

const hash=p=>createHash('sha256').update(readFileSync(new URL(p,import.meta.url))).digest('hex');writeFileSync(new URL('../../build-web/parity/fx-layout-report.json',import.meta.url),JSON.stringify({passed:true,effects:4,parameters:Object.values(fxLayouts).reduce((n,p)=>n+p.controls.flat().filter(id=>id!=='TAP').length+(p.body?.length||0)+p.knobs.flat().length,0),nativePanelSha256:hash('../../src/plugin/ui/FxPanel.cpp'),nativeHeaderSha256:hash('../../src/plugin/ui/FxPanel.h'),layoutModuleSha256:hash('../fx-editor-layout.js')},null,2));
