// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {distortionCharacterNames,distortionCharacterText,fxEditorFields} from '../fx-panel.js';
const lines=readFileSync(new URL('../../build-web/parity/fx-ui-native.tsv',import.meta.url),'utf8').trim().split(/\r?\n/);assert.equal(lines.length,9009);
for(const line of lines){const[m,i,name,text]=line.split('\t');assert.equal(distortionCharacterNames[+m],name);assert.equal(distortionCharacterText(+m,+i/1000),text,`Mode ${m}, value ${i}/1000`);}
const descriptors=['dly_time','dly_wow','dly_flutter'].map(id=>({id,group:'DELAY'})),effects=['','','','','','','DELAY'];let values={dly_sync:3,dly_character:0};let fields=fxEditorFields(descriptors,6,effects,{},0,id=>values[id]);assert(fields.every(d=>d.dim()));values={dly_sync:0,dly_character:1};fields=fxEditorFields(descriptors,6,effects,{},0,id=>values[id]);assert(fields.every(d=>!d.dim()));
console.log('FX UI parity: 9,009 native C++ character labels/value strings match; native DELAY sync/tape visual states match.');

const hash=p=>createHash('sha256').update(readFileSync(new URL(p,import.meta.url))).digest('hex');writeFileSync(new URL('../../build-web/parity/fx-ui-parity-report.json',import.meta.url),JSON.stringify({passed:true,checks:9009,nativeSourceSha256:hash('../../src/fx/Distort.cpp'),uiModuleSha256:hash('../fx-panel.js')},null,2));
