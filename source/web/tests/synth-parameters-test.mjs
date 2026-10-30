// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {operatorValue} from '../controls.js';
import {operatorParameter,operatorRatioText,voiceFilterState,filterParameter} from '../synth-parameters.js';
const meta=JSON.parse(readFileSync(new URL('../../build-web/dist/metadata.json',import.meta.url),'utf8'));
const errors=[];let checks=0;
for(const line of readFileSync(new URL('../../build-web/parity/synth-ui-native.tsv',import.meta.url),'utf8').trim().split(/\r?\n/)){
  const [tag,...p]=line.split('\t');
  if(tag==='M'){
    const [mode,fx,which,value,name,list,text]=p,spec=operatorValue(+mode,+which,+fx,meta.models);checks++;
    if(!name){assert(!spec);continue;}
    const actual=spec.choices?spec.choices[(+value)%spec.choices.length]:spec.text(+value);
    if(spec.name!==name||!!spec.choices!==!!(+list)||actual!==text)errors.push({mode,fx,which,value,name,actualName:spec.name,text,actual});
  }else if(tag==='R'){
    const [coarse,fine,detune,trans,freq,fixed_hz,text]=p,values={coarse:+coarse,fine:+fine,detune:+detune,trans:+trans,freq:+freq,fixed_hz:+fixed_hz};assert.equal(operatorRatioText(id=>values[id],''),text);checks++;
  }else if(tag==='F'){
    const [type,sat,cmos,casc]=p;for(const on of [false,true]){const s=voiceFilterState(+type,on);assert.equal(s.saturating,!!+sat);assert.equal(s.cmos,!!+cmos);assert.equal(s.casc,!!+casc);assert.equal(s.driveDim,!on||!+sat);assert.equal(filterParameter({id:'cutoff'},s).dim,!on);}checks+=2;
  }
}
assert.deepEqual(errors.slice(0,12),[],`${errors.length} native mode UI mismatches`);
for(let op=1;op<=6;op++){
  assert.equal(operatorParameter({id:`op${op}_bright`}).text(20),'+20 %');assert.equal(operatorParameter({id:`op${op}_stiff`}).text(-20),'-20 %');assert.equal(operatorParameter({id:`op${op}_density`}).text(32),'32 modes');
  assert.equal(operatorParameter({id:`op${op}_fixed_hz`}).text(44000),'440.0 Hz');assert.equal(operatorParameter({id:`op${op}_fixed_hz`}).text(200000),'2.00 kHz');
}
const hash=p=>createHash('sha256').update(readFileSync(new URL(p,import.meta.url))).digest('hex');
writeFileSync(new URL('../../build-web/parity/synth-parameters-report.json',import.meta.url),JSON.stringify({passed:true,checks,nativeTextSha256:hash('../../src/plugin/ui/PatchText.cpp'),nativePanelSha256:hash('../../src/plugin/ui/Panels.cpp'),nativeFilterSha256:hash('../../src/engines/fm/FmFilters.h'),panelModuleSha256:hash('../synth-panel.js'),controlsSha256:hash('../controls.js'),parametersSha256:hash('../synth-parameters.js')},null,2));
console.log(`${checks} native operator mode readouts/visibility and voice-filter state checks passed; MODAL units and Fixed Hz display passed.`);
