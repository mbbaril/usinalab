// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {configureControls,displayValue,parameterCaption} from '../controls.js';
import {operatorParameter,filterParameter,voiceFilterState} from '../synth-parameters.js';
const meta=JSON.parse(readFileSync(new URL('../../build-web/dist/metadata.json',import.meta.url),'utf8'));configureControls(meta);
const ids=new Map(meta.parameters.map(d=>[d.id,d])),errors=[];let checks=0;
for(const row of readFileSync(new URL('../../build-web/parity/panel-ui-native.tsv',import.meta.url),'utf8').trim().split(/\r?\n/)){
  const [id,value,caption,text]=row.split('\t'),d=id.startsWith('op')?operatorParameter(ids.get(id)):filterParameter(ids.get(id),voiceFilterState(11,true));
  assert(d,id);const actual=displayValue(d,+value),name=d.uiCaption??parameterCaption(d);checks++;
  if(actual!==text||name!==caption)errors.push({id,value,caption,name,text,actual});
}
assert.deepEqual(errors.slice(0,20),[],`${errors.length} mismatches against actual JUCE panel Knob labels`);
const hash=p=>createHash('sha256').update(readFileSync(new URL(p,import.meta.url))).digest('hex');
writeFileSync(new URL('../../build-web/parity/panel-readouts-report.json',import.meta.url),JSON.stringify({passed:true,checks,nativePanelSha256:hash('../../src/plugin/ui/Panels.cpp'),nativeControlsSha256:hash('../../src/plugin/ui/Controls.cpp'),controlsSha256:hash('../controls.js'),parametersSha256:hash('../synth-parameters.js'),scope:'Readouts/captions from real OperatorPanel and FilterPanel knobs, including envelope controls, with their attached parameter ranges; not DSP parity.'},null,2));
console.log(`${checks} readouts/captions match actual native OperatorPanel/FilterPanel controls.`);
