// SPDX-License-Identifier: GPL-3.0-only
import {operatorValue} from './controls.js';
import {operatorParameter} from './synth-parameters.js';
export const operatorSections={
  oscillator:[['freq','coarse'],['fine','detune','trans'],['fixed_hz','eg1','lfo1_pitch']],
  expression:[['level','vel','vel_pitch','lfo1_level'],['VEL_COARSE'],['kt_low','kt_high','kt_center',['kt_l_curve','kt_h_curve']]],
  envelope:[['atk','dec','sus','rel','curve','rate_key'],['env_atk_curve','env_rel_curve',['env_mode','env_retrig']]]
};
export const modalRows={object:[['c','d','bright','stiff'],['position','density','irreg','tension'],['rattle','material']],exciter:[['exciter'],['strike','hardness','pressure','b']]};
export const operatorUsesWave=mode=>![6,7,9,10,11].includes(mode);
export function modeRows(mode,fx,models,page=0){
  const specs=['a','b','c','d'].map((key,i)=>({key,spec:operatorValue(mode,i,fx,models)})).filter(v=>v.spec);
  const selectors=['mode',...specs.filter(v=>v.spec.choices).map(v=>v.key)];
  if(mode===8)return {selectors,rows:page?modalRows.exciter:modalRows.object};
  const knobs=specs.filter(v=>!v.spec.choices).map(v=>v.key),rows=[];for(let i=0;i<knobs.length;i+=3)rows.push(knobs.slice(i,i+3));
  return {selectors,rows};
}
export function mountOperatorRows({root,el,prefix,rows,descriptor,field,special,resolve=operatorParameter,choices=new Set(),columns}){
  const layout=el('div','operator-native-layout');root.append(layout);
  const append=(parent,key)=>{if(key==='VEL_COARSE'){if(special)parent.append(special);return;}
    if(Array.isArray(key)){const stack=el('div','choice-controls operator-choice-stack');key.forEach(id=>append(stack,id));parent.append(stack);return;}
    const d=descriptor(prefix+key);if(d)parent.append(field(resolve(d,key)));
  };
  rows.forEach((keys,index)=>{const categorical=keys.every(k=>typeof k==='string'&&choices.has(k)),row=el('div',categorical?'choice-controls operator-native-controls':'deep-controls operator-native-row');row.dataset.operatorRow=index;row.dataset.columns=columns||keys.length;row.style.setProperty('--operator-columns',columns||keys.length);keys.forEach(key=>append(row,key));layout.append(row);});
  return layout;
}
export function mountOperatorMode({root,el,prefix,mode,fx,models,modalPage,descriptor,field,subtabs,onModalPage,showWave=true}){
  const plan=modeRows(mode,fx,models,modalPage),resolve=(d,key)=>{const i=['a','b','c','d'].indexOf(key);return operatorParameter(i>=0?{...d,...operatorValue(mode,i,fx,models)}:{...d,choices:key==='material'?models.material:key==='exciter'?models.exciter:undefined,dim:key==='wave'&&!operatorUsesWave(mode)});};
  mountOperatorRows({root,el,prefix,rows:[plan.selectors,...(showWave?[['wave']]:[])],choices:new Set([...plan.selectors,'wave']),descriptor,field,resolve});
  if(mode===8){root.classList.add('operator-modal-mode');const tabs=subtabs(['OBJECT','EXCITER'],modalPage,onModalPage);tabs.classList.add('operator-mode-tabs');root.append(tabs);}
  const layout=mountOperatorRows({root,el,prefix,rows:plan.rows,choices:new Set(['exciter']),columns:mode===8?4:3,descriptor,field,resolve});
  layout.dataset.operatorSurface=mode===8?(modalPage?'exciter':'object'):'values';
  if(mode===8&&!modalPage){const material=layout.querySelector?.(`[data-param="${prefix}material"]`);if(material)material.classList.add('operator-material');}
  return layout;
}
