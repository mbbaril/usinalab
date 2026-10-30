// SPDX-License-Identifier: GPL-3.0-only
import {nativeFixed,envelopeTimeText} from './controls.js';
// Display-only laws and filter state from OperatorPanel/FilterPanel in Panels.cpp.
const pct=v=>`${v} %`,signedPct=v=>`${v>0?'+':''}${v} %`;
export function operatorRatioText(param,prefix) {
  if(param(prefix+'freq')){const hz=Math.max(1,param(prefix+'fixed_hz'))/100;return hz<1000?`${hz.toFixed(2)} Hz`:`${(hz/1000).toFixed(3)} kHz`;}
  const coarse=param(prefix+'coarse'),ratio=(coarse<7?2**(coarse-7):coarse-6)*param(prefix+'fine')/100,trans=param(prefix+'trans'),detune=param(prefix+'detune');
  return `x${nativeFixed(ratio,ratio<1?4:3)}${trans?' '+(trans>0?'+':'')+trans+'st':''}${detune?' '+(detune>0?'+':'')+detune+'c':''}`;
}
export function envelopeParameter(d) {
  const key=d.id.replace(/^(op\d_env|eg[12]|env1)_/,'');
  const captions={atk:'ATTACK',dec:'DECAY',sus:'SUSTAIN',rel:'RELEASE',curve:'CURVE',atk_curve:'ATK CURVE',rel_curve:'REL CURVE',mode:'MODE',retrig:'RETRIG'};
  const result={...d,uiCaption:captions[key]??d.uiCaption};
  if(['atk','dec','rel'].includes(key))result.text=envelopeTimeText;
  if(key==='sus')result.text=pct;
  if(key==='atk_curve')result.text=v=>v?String(v):'LIN';
  if(key==='rel_curve')result.text=v=>v<0?'= DEC':v===10?'EXP':v?String(v):'LIN';
  return result;
}
export function operatorParameter(d) {
  const key=d.id.replace(/^op\d_/,'');
  d={...d,uiCaption:({atk:'ATTACK',dec:'DECAY',sus:'SUSTAIN',rel:'RELEASE',kt_center:'KT CENTER',lfo1_pitch:'LFO1 PIT',lfo1_level:'LFO1 LVL',env_atk_curve:'ATK CURVE',env_rel_curve:'REL CURVE',env_mode:'MODE',env_retrig:'RETRIG'})[key]??d.uiCaption};
  if(key.startsWith('env_'))return envelopeParameter(d);
  if(['atk','dec','rel'].includes(key))return {...d,text:envelopeTimeText};
  if(['vel','sus','rate_key'].includes(key))return {...d,text:pct};
  if(['vel_pitch','lfo1_level','kt_low','kt_high'].includes(key))return {...d,text:signedPct};
  if(key==='fine')return {...d,text:v=>`x${nativeFixed(v/100,2)}`};
  if(key==='detune')return {...d,text:v=>`${v>0?'+':''}${v} c`};
  if(['eg1','lfo1_pitch'].includes(key))return {...d,text:v=>`${v>0?'+':''}${nativeFixed(v/16,1)} st`};
  if(['kt_l_curve','kt_h_curve'].includes(key))return {...d,choices:['LIN','EXP']};
  if(key==='coarse')return {...d,choices:['1/128','1/64','1/32','1/16','1/8','1/4','1/2',...Array.from({length:32},(_,i)=>String(i+1))]};
  if(['bright','stiff'].includes(key))return {...d,text:signedPct};
  if(key==='density')return {...d,text:v=>`${v} modes`};
  if(['position','irreg','strike','hardness','pressure','tension','rattle'].includes(key))return {...d,text:pct};
  if(key==='fixed_hz')return {...d,text:v=>{const hz=Math.max(1,v)/100;return hz<1000?`${hz.toFixed(hz<10?2:1)} Hz`:`${(hz/1000).toFixed(2)} kHz`;}};
  return d;
}
export function voiceFilterState(type,on) {
  type=((type%18)+18)%18;
  const casc=type===11,cmos=type>=14,saturating=[2,3,6,11,12,13].includes(type);
  return {type,casc,cmos,saturating,dim:!on,driveDim:!on||!saturating,special:casc?['hp_cut','hp_peak','casc_fb','hp_eg2_int','casc_out','casc_vca','casc_source','casc_hot']:cmos?['cmos_life']:[]};
}
export function filterParameter(d,state) {
  if(/^eg[12]_(atk|dec|sus|rel|curve|atk_curve|rel_curve|mode|retrig)$/.test(d.id))return envelopeParameter(d);
  const captions={out_gain:'OUT GAIN',lpg_mode:'LPG',lpg_decay:'DECAY',lpg_color:'COLOR',filter:'FILTER',filter_type:'TYPE',cutoff:'CUTOFF',reso:'RESO',flt_drive:'DRIVE',eg2_int:'EG2 INT',key_low:'KEY LOW',key_high:'KEY HIGH',key_center:'KEY CTR',hp_cut:'HP CUT',hp_peak:'HP PEAK',casc_fb:'CASC FB',hp_eg2_int:'HP EG2',casc_out:'CASC OUT',casc_vca:'CASC VCA',casc_source:'VCA SOURCE',casc_hot:'CASC HOT',cmos_life:'CMOS LIFE',lpg_source:'SOURCE',lpg_r_mode:'RIGHT',lpg_r_source:'R SOURCE',lpg_r_decay:'R DECAY',f2_route:'ROUTE',f2_type:'TYPE',f2_cutoff:'CUTOFF',f2_reso:'RESO',f2_drive:'DRIVE',f2_eg2_int:'EG2 INT',f2_key:'KEY',f2_mix:'MIX'};
  const result={...d,uiCaption:captions[d.id]??d.uiCaption,dim:d.id==='filter'?false:d.id==='flt_drive'?state.driveDim:state.dim};
  if(d.id==='casc_vca')result.choices=['GATE','EG2','ENV','SOURCE'];
  if(d.id==='lpg_mode')result.choices=['OFF','PING','EG2','SOURCE'];
  if(d.id==='lpg_r_mode')result.choices=['R SAME','R PING','R EG2','R SOURCE'];
  if(d.id==='lpg_r_decay')result.text=v=>`${v>0?'+':''}${v}`;
  if(d.id==='f2_route')result.choices=['OFF','SERIES','PARALLEL','STEREO'];   // FILTER 2: dim while OFF
  if(/^f2_/.test(d.id))result.dim=d.id!=='f2_route'&&!state.f2on;
  if(['f2_reso','f2_mix'].includes(d.id))result.text=pct;
  if(d.id==='f2_key')result.text=signedPct;
  if(['filter','lofi','casc_hot'].includes(d.id))result.compactToggle=true;
  if(['lpg_decay','lpg_color'].includes(d.id))result.text=pct;
  if(['out_gain','lpg_mode','lpg_decay','lpg_color','lofi','lpg_source','lpg_r_mode','lpg_r_source','lpg_r_decay'].includes(d.id))result.dim=false;
  if(['flt_drive','f2_drive'].includes(d.id))result.text=v=>`x${nativeFixed(2**(3*v/100),2)}`;
  if(['casc_out','out_gain'].includes(d.id))result.text=v=>`+${nativeFixed(.75*v,2)} dB`;
  if(['cutoff','hp_cut','f2_cutoff'].includes(d.id))result.text=v=>{const hz=6.489*(23680/6.489)**(v/1000);return hz<1000?`${hz<100?hz.toFixed(1):nativeFixed(hz,3).replace(/\.?0+$/,'')} Hz`:`${nativeFixed(hz/1000,2)} kHz`;};
  if(['key_low','key_high','casc_fb'].includes(d.id))result.text=signedPct;
  if(['reso','hp_peak','cmos_life'].includes(d.id))result.text=pct;
  if(['eg2_int','hp_eg2_int','f2_eg2_int'].includes(d.id))result.text=v=>`${v>0?'+':''}${nativeFixed(v/16,1)} st`;
  return result;
}
