// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {nativeFunction,nativeKnobRows,nativeRow} from './native-source.mjs';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {filterRows,mountFilterRows} from '../filter-layout.js';
import {voiceFilterState} from '../synth-parameters.js';
const cpp=nativeFunction(readFileSync(new URL('../../src/plugin/ui/Panels.cpp',import.meta.url),'utf8'),'void FilterPanel::resized()');
const aliases={cutoff:'cutoff',reso:'reso',drive:'flt_drive',eg2Int:'eg2_int',keyLow:'key_low',keyHigh:'key_high',keyCenter:'key_center',offset:'flt_offset',hpCut:'hp_cut',hpPeak:'hp_peak',cascFb:'casc_fb',hpEg2Int:'hp_eg2_int',cascOut:'casc_out',atk:'eg2_atk',dec:'eg2_dec',sus:'eg2_sus',rel:'eg2_rel',curve:'eg2_curve',f2Cut:'f2_cutoff',f2Reso:'f2_reso',f2Drive:'f2_drive',f2Eg2:'f2_eg2_int',f2Key:'f2_key',f2Mix:'f2_mix'};
const rows=nativeKnobRows(cpp),row=member=>nativeRow(rows,member).map(name=>{assert(name in aliases,`Missing filter alias ${name}`);return aliases[name];});
assert.deepEqual(filterRows.common[1],row('cutoff'));assert.deepEqual(filterRows.cascade[0].slice(0,5),row('hpCut'));assert.deepEqual(filterRows.envelope[0],row('atk'));assert.deepEqual(filterRows.filter2[1],row('f2Cut'));
assert(cpp.indexOf('outGain.setBounds')<cpp.indexOf('lofi.setBounds'));assert(cpp.indexOf('lpgMode.setBounds')<cpp.indexOf('lpgDecay.setBounds'));
const meta=JSON.parse(readFileSync(new URL('../../build-web/dist/metadata.json',import.meta.url),'utf8')),el=()=>({children:[],dataset:{},style:{setProperty(){}},append(...n){this.children.push(...n);}});
const filterType=meta.parameters.find(d=>d.id==='filter_type');assert(filterType);let configurations=0;
for(let type=filterType.min;type<=filterType.max;type+=filterType.step)for(const on of [false,true]){
  configurations++;const state=voiceFilterState(type,on),all=[];
  for(const key of ['common',...(state.casc?['cascade']:state.cmos?['cmos']:[]),'envelope','output','lpg','filter2']){
    const ids=[],root=el();mountFilterRows({root,el,rows:filterRows[key],state,descriptor:id=>meta.parameters.find(d=>d.id===id),field:d=>{ids.push(d.id);if(d.id==='eg2_atk')assert.equal(d.dim,!on);if(d.id==='eg2_atk_curve'||['out_gain','lofi','lpg_mode','lpg_decay','lpg_color','lpg_source','lpg_r_mode','lpg_r_source','lpg_r_decay'].includes(d.id))assert(!d.dim);return el();}});assert.deepEqual(ids,filterRows[key].flat(Infinity));all.push(...ids);
  }
  assert.equal(new Set(all).size,all.length);assert.equal(all.includes('hp_cut'),state.casc);assert.equal(all.includes('cmos_life'),state.cmos);
}
const hash=p=>createHash('sha256').update(readFileSync(new URL(p,import.meta.url))).digest('hex');writeFileSync(new URL('../../build-web/parity/filter-layout-report.json',import.meta.url),JSON.stringify({passed:true,configurations,nativePanelSha256:hash('../../src/plugin/ui/Panels.cpp'),layoutModuleSha256:hash('../filter-layout.js'),layoutSha256:hash('../filter-layout.css')},null,2));
console.log(`Filter native rows and ${configurations} topology/power states: visibility, independent OUTPUT/LPG, unique parameter addressing passed.`);
