// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {operatorSections,modalRows,modeRows,operatorUsesWave,mountOperatorRows} from '../operator-layout.js';
import {operatorValue} from '../controls.js';
const native=readFileSync(new URL('../../src/plugin/ui/Panels.cpp',import.meta.url),'utf8'),meta=JSON.parse(readFileSync(new URL('../../build-web/dist/metadata.json',import.meta.url),'utf8'));
const modal=native.match(/\? std::vector<Knob\*> \{([^}]+)\}\s*:\s*std::vector<Knob\*> \{([^}]+)\}/);
assert(modal);const vars={cKnob:'c',dKnob:'d',bKnob:'b',modalDamp:'damp',opWidth:'width'},mk=['bright','stiff','position','density','irreg','strike','hardness','pressure','tension','rattle'];
for(const [page,chunk]of [['object',modal[1]],['exciter',modal[2]]]){const keys=[...chunk.matchAll(/&(\w+)(?:\[(\d+)\])?/g)].map(m=>m[1]==='mk'?mk[+m[2]]:vars[m[1]]);assert.deepEqual(modalRows[page].flat().filter(k=>!['material','exciter'].includes(k)),keys);}
assert(native.includes('i % 4 == 0'));assert(native.includes('i % 3 == 0'));
const resized=native.split('void OperatorPanel::resized()')[1].split('// --- ModPanel')[0];
for(const keys of [['freq','coarse'],['fine','detune','trans']]){let last=-1;for(const key of keys){const at=resized.indexOf(`${key}.setBounds`);assert(at>last);last=at;}}
assert.deepEqual(operatorSections.expression[0],['level','vel','vel_pitch','lfo1_level']);assert(resized.includes('level.setBounds')&&resized.indexOf('velCoarse.setBounds')>resized.indexOf('lfo1Level.setBounds'));
let cases=0;
for(let mode=0;mode<12;mode++)for(let fx=0;fx<(mode===5?17:1);fx++)for(let page=0;page<(mode===8?2:1);page++){
  const plan=modeRows(mode,fx,meta.models,page),flat=[...plan.selectors,...plan.rows.flat()],expected=['mode'];
  for(let i=0;i<4;i++)if(operatorValue(mode,i,fx,meta.models))expected.push(['a','b','c','d'][i]);
  if(mode!==8)assert.deepEqual([...flat].sort(),expected.sort());else {assert(flat.includes('a'));assert.equal(flat.includes('b'),!!page);assert.equal(flat.includes('c'),!page);assert.equal(flat.includes('d'),!page);assert.equal(flat.includes('material'),!page);assert.equal(flat.includes('exciter'),!!page);}
  assert.equal(new Set(flat).size,flat.length);for(const k of flat)assert(meta.parameters.some(d=>d.id==='op1_'+k));cases++;
}
assert.deepEqual(Array.from({length:12},(_,i)=>operatorUsesWave(i)),[true,true,true,true,true,true,false,false,true,false,false,false]);
const el=(tag,cls='')=>({children:[],dataset:{},style:{setProperty(){}},append(...nodes){this.children.push(...nodes);}}),all=meta.parameters;
for(let op=1;op<=6;op++)for(const rows of Object.values(operatorSections)){
  const root=el('section'),got=[],special=el('button');mountOperatorRows({root,el,prefix:`op${op}_`,rows,descriptor:id=>all.find(d=>d.id===id),field:d=>{got.push(d.id);return el('label');},special});
  assert.deepEqual(got,rows.flat(Infinity).filter(k=>k!=='VEL_COARSE').map(k=>`op${op}_${k}`));
}
const hash=p=>createHash('sha256').update(readFileSync(new URL(p,import.meta.url))).digest('hex');writeFileSync(new URL('../../build-web/parity/operator-layout-report.json',import.meta.url),JSON.stringify({passed:true,modeConfigurations:cases,operators:6,nativePanelSha256:hash('../../src/plugin/ui/Panels.cpp'),layoutModuleSha256:hash('../operator-layout.js')},null,2));
console.log('Operator layout: six operators, native OSC/LEVEL/ENV order, 12 modes/17 effects, MODAL OBJECT/EXCITER visibility and native wave state passed.');
