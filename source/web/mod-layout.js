// SPDX-License-Identifier: GPL-3.0-only
import {mountOperatorRows} from './operator-layout.js';
import {envelopeParameter} from './synth-parameters.js';
import {envelopeTimeText,nativeFixed} from './controls.js';
export const modRows={
 envelope:[['atk','dec','sus','rel','curve'],['atk_curve','rel_curve',['mode','retrig']]],
 lfo:[['wave','key_sync','sync'],['wave_b','polarity','retrig'],['speed','phase','fade','symmetry','morph']],
 func:[['mode','polarity'],['speed','smooth','sustain'],['sync','retrig']],
 rand:[['type'],['sync','retrig'],['speed','a','b'],['rise','fall']],
 comb:[['type'],['source'],['mod'],['amount']]
};
const choices=new Set(['wave','key_sync','sync','wave_b','polarity','retrig','mode','type','source','mod']);
export const combUsesMod=type=>[0,1,2,3,4,10,11].includes(type);
export function modParameter(d,param){
  if(/^macro[1-4]$/.test(d.id))return {...d,uiCaption:`MACRO ${d.id.slice(-1)}`};if(/^a[1-3]_(depth|target)$/.test(d.id))return {...d,uiCaption:d.id.replace('_',' ').toUpperCase(),...(/_depth$/.test(d.id)?{text:v=>`${v>0?'+':''}${v} %`}:{})};
  const match=d.id.match(/^(lfo|func|rand|comb)(\d)_(.+)$/);if(!match)return envelopeParameter(d);
  const[,family,k,key]=match,prefix=`${family}${k}_`,caption={key_sync:'KEY SYNC',wave_b:'WAVE B',fade:'FADE IN'}[key]??key.toUpperCase();let result={...d,uiCaption:caption};
  result.dim=()=>family==='func'?(key==='speed'?!!param(prefix+'sync'):key==='sustain'?![2,3].includes(param(prefix+'mode')):false):family==='rand'?key==='speed'&&!!param(prefix+'sync'):family==='comb'?(key==='mod'?!combUsesMod(param(prefix+'type')):key==='source'?param(prefix+'type')===12:false):family==='lfo'?(key==='speed'?!!param(prefix+'sync'):key==='fade'?!param(prefix+'key_sync'):key==='wave_b'?!param(prefix+'morph'):false):false;
  if(key==='key_sync')result.choices=['OFF','COMMON','VOICE'];
  if(key==='sync')result.choices=['FREE (Hz)','1/32','1/16T','1/16','1/16D','1/8T','1/8','1/8D','1/4T','1/4','1/4D','1/2','1 BAR','2 BARS','4 BARS'];
  if(key==='speed')result.text=v=>{const hz=.01*10000**(v/127);return `${nativeFixed(hz,hz<10?2:1)} Hz`;};
  if(key==='fade'||key==='rise'||key==='fall')result.text=envelopeTimeText;
  if(key==='morph')result.text=v=>`${v} %`;if(key==='symmetry'||family==='comb'&&key==='amount')result.text=v=>`${v>0?'+':''}${v} %`;
  if(family==='rand'&&(key==='a'||key==='b')){const type=param(prefix+'type');result.uiCaption=key==='a'?(type===0?'FLIP':'PROBA'):(type===0?'LENGTH':'CORREL');result.text=v=>key==='b'&&type===0?`${2+Math.floor(v*14/100)} steps`:`${v} %`;}
  return result;
}
export function mountModRows({root,el,family,prefix,param,descriptor,field}){
  const rows=modRows[family].map(keys=>family==='rand'&&param(prefix+'type')===1?keys.filter(k=>k!=='a'&&k!=='b'):keys);
  const layout=mountOperatorRows({root,el,prefix,rows,choices,descriptor,field,resolve:d=>modParameter(d,param)});layout.classList.add('mod-native-layout');
  if(family==='rand')for(const row of [...layout.children].slice(2)){row.dataset.columns=3;row.style.setProperty('--operator-columns',3);}
  if(family==='comb'){const row=layout.children[3];row.dataset.columns=3;row.style.setProperty('--operator-columns',3);}
  return layout;
}

export function mountMacroRows({root,el,param,descriptor,field}){
  const resolve=d=>modParameter(d,param);mountOperatorRows({root,el,prefix:'',rows:[['macro1','macro2','macro3','macro4']],descriptor,field,resolve}).classList.add('mod-native-layout');
  const targets=el('div','mod-macro-targets');for(let k=1;k<=3;k++){const column=el('div','mod-macro-column');mountOperatorRows({root:column,el,prefix:'',rows:[[`a${k}_depth`],[`a${k}_target`]],choices:new Set([`a${k}_target`]),descriptor,field,resolve}).classList.add('mod-native-layout');targets.append(column);}root.append(targets);
}

export const combFormulas=["source + mod x amount","source - mod x amount","source x (1 - amount + mod x amount)","source / (1 + |mod x amount| x 3)","source x (1 - amount) + mod x amount","source, slowed (amount: 0 .. 2 s)","1 when source > amount, else 0","source + amount","source bent (amount: < 0 log, > 0 exp)","source's level, followed (amount: its release)","the lower of source and mod x amount","the higher of source and mod x amount","the audio input's level (amount: sensitivity)","bit (amount x 7) of source's 8-bit code, as 0 / 1","source's code XOR mod's code, top |amount| x 8 bits","source's code, its top (amount > 0) or low bits kept","(source's code + mod x amount x 128) mod 256"];
export const randomCaptions=['a loop of steps, each changed as it comes round with the chance FLIP','a new random value each tick','high or low each tick: PROBA high, CORREL keeps the last'];
export const functionSustain=(mode,value,count)=>[2,3].includes(mode)?Math.max(0,Math.min(count-1,value)):-1;

export const controllerNumberText=(kind,n)=>n<0?'NONE':kind==='cc'?`CC ${n}`:`${['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'][n%12]}${Math.floor(n/12)-1} (${n})`;
