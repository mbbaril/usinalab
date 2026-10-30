// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {mountOperatorOverview} from '../operator-overview.js';
import {operatorUsesWave,mountOperatorMode} from '../operator-layout.js';
const native=readFileSync(new URL('../../src/plugin/ui/Panels.cpp',import.meta.url),'utf8'),meta=JSON.parse(readFileSync(new URL('../../build-web/dist/metadata.json',import.meta.url),'utf8'));
const stripSource=native.split('OperatorStrip::OperatorStrip')[1].split('// --- OperatorPanel')[0];assert(stripSource.indexOf('mode.setBounds')<stripSource.indexOf('wave.setBounds'));assert(stripSource.includes('mode.setTitleVisible(false)')&&stripSource.includes('wave.setTitleVisible(false)'));
const el=()=>({children:[],dataset:{},style:{setProperty(){}},append(...n){this.children.push(...n);}});let cases=0;
const operatorMode=meta.parameters.find(d=>d.id==='op1_mode');assert(operatorMode);
for(let op=1;op<=6;op++)for(let mode=operatorMode.min;mode<=operatorMode.max;mode+=operatorMode.step){const got=[],strip=el(),root=mountOperatorOverview({el,strip,prefix:`op${op}_`,mode,descriptor:id=>meta.parameters.find(d=>d.id===id),field:d=>{got.push(d);return {querySelector:()=>({setAttribute(k,v){assert.equal(k,'aria-label');assert(v.startsWith(`OP${op} `));}})};}});assert.deepEqual(got.map(d=>d.id),[`op${op}_mode`,`op${op}_wave`]);assert.equal(got[1].dim,!operatorUsesWave(mode));assert(got.every(d=>d.instance==='overview'));assert.equal(root.children.length,2);cases++;}
const css=readFileSync(new URL('../synth-layout.css',import.meta.url),'utf8');assert(css.includes('minmax(0,3fr) minmax(0,4fr) minmax(0,6fr) minmax(0,3fr)'));assert(css.includes('--dial-size:46px'));assert(/kKnobDiameter = 46/.test(readFileSync(new URL('../../src/plugin/ui/Controls.h',import.meta.url),'utf8')));
const hash=p=>createHash('sha256').update(readFileSync(new URL(p,import.meta.url))).digest('hex');writeFileSync(new URL('../../build-web/parity/operator-overview-report.json',import.meta.url),JSON.stringify({passed:true,cases,nativePanelSha256:hash('../../src/plugin/ui/Panels.cpp'),moduleSha256:hash('../operator-overview.js'),layoutSha256:hash('../synth-layout.css')},null,2));
console.log(`Operator overview: ${cases} operator/mode configurations, native MODE/WAVE order/title hiding, correct wave dim states and native 46px pointer dial unit passed.`);
