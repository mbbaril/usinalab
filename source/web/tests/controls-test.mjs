// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {nativeStrings} from './native-source.mjs';
import create from '../../build-web/usina.mjs';
import {controlChoices,displayValue,operatorValue,sourceColor,configureControls} from '../controls.js';
const wasm=await create(),meta=JSON.parse(wasm.UTF8ToString(wasm._web_schema()));
configureControls(meta);
const byId=new Map(meta.parameters.map(d=>[d.id,d]));
assert.deepEqual(controlChoices(byId.get('op1_freq')),['RATIO','FIXED']);
assert.equal(controlChoices(byId.get('op1_coarse'))[7],'× 1');
assert.equal(controlChoices(byId.get('mpe_bend_range'))[0],'1 st');assert.equal(controlChoices(byId.get('mpe_bend_range')).at(-1),'96 st');
assert.equal(displayValue(byId.get('op1_fixed_hz'),44000),'440.00 Hz');
assert.equal(displayValue(byId.get('op1_fine'),100),'× 1.00');
assert.deepEqual(controlChoices(byId.get('tex_media')),['Digital','Vintage','Tape','Worn Tape']);
for(const d of meta.parameters){const c=controlChoices(d);if(c)assert.equal(c.length,d.max-d.min+1,d.id);}
for(const d of meta.destinations)assert(byId.has(d.id),`Unknown route parameter ${d.id}`);
const counts=[2,2,4,3,3,4,0,0,4,4,4,4];
for(let mode=0;mode<12;++mode){const values=[0,1,2,3].map(w=>operatorValue(mode,w,0,meta.models));assert.equal(values.filter(Boolean).length,counts[mode],`operator mode ${mode}`);for(const d of values.filter(Boolean)){assert(d.name);if(d.choices)assert(d.choices.length);else assert(d.text(50));}}
const nativeText=readFileSync(new URL('../../src/plugin/ui/PatchText.cpp',import.meta.url),'utf8'),effectNames=nativeStrings(nativeText,'kEffects');
for(let fx=0;fx<effectNames.length;++fx)for(const which of [2,3])assert(operatorValue(5,which,fx).text(100));
assert.equal(operatorValue(8,0,0,meta.models).choices.length,nativeStrings(nativeText,'kOpFilters').length);
assert(meta.vaporKnobs.length>0);assert.equal(meta.vaporKnobs[3][1],'WARP');assert.equal(meta.vaporKnobs[5][1],'CHORD');
assert.equal(sourceColor('lfo1'),'#25e0c4');assert.equal(sourceColor('mac1'),'#ff5ca8');
assert.equal(meta.sources.filter(s=>s.id==='timbre').length,1);
console.log('Semantic controls: Ratio/Fixed, native units, 12 operator modes, 17 operator effects, all route IDs, Vapor labels and family palette: pass');
