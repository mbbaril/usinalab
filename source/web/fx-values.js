// SPDX-License-Identifier: GPL-3.0-only
import nativeText from './fx-native-text.js';
// JUCE roundToInt uses ties-to-even; using Math.round would change native readouts.
const roundNative = v => {
  const low=Math.floor(v), fraction=v-low;
  return fraction===.5 ? low+(low%2!==0?1:0) : Math.round(v);
};
export function fxValueFormatter(id) {
  if (nativeText[id]) return v => nativeText[id][Math.max(0,Math.min(1000,Math.round(v*1000)))];
  if(id==='feedback')return v=>Math.fround(v).toFixed(3);
  if(id==='fb_time')return v=>Math.fround(v).toFixed(1)+' ms';
  if(['fx_a_mix','fx_b_mix','aux_send'].includes(id))return v=>`${roundNative(Math.fround(Math.fround(v)*100))} %`;
  if (id==='dly_time') return v=>v<1000?`${v<100?v.toFixed(1):String(Number(v.toFixed(1)))} ms`:`${Math.fround(Math.fround(v)/1000).toFixed(2)} s`;
  if (id==='dly_lowcut') return v=>v<=20.5?'OFF':`${roundNative(v)} Hz`;
  if (id==='dly_highcut') return v=>v>=19999?'OFF':`${Math.fround(Math.fround(v)/1000).toFixed(1)} kHz`;
  if (['dly_out','dist_drive','dist_out','aux_return','fxf_out'].includes(id)) return v=>`${v.toFixed(1)} dB`;
  if (id==='fxf_cutoff') return v=>v<1000?`${roundNative(v)} Hz`:`${Math.fround(Math.fround(v)/1000).toFixed(2)} kHz`;
  if (['fxf_spread','fxf_follow'].includes(id)) return v=>{const p=roundNative(Math.fround(Math.fround(v)*100));return `${p>0?'+':''}${p} %`;};
  if (['dist_sym','dist_tone'].includes(id)) return v=>{const p=roundNative(Math.fround(Math.fround(v)*100));return `${p>0?'+':''}${p}`;};
  if (/^(tex_(time|shape|feedback|drywet|reverb)|vap_(position|size|density|texture|drywet|spread|feedback|reverb)|dly_(feedback|wow|flutter|mix)|fxf_(reso|mix)|dist_mix|rev_amount)$/.test(id)) return v=>`${roundNative(Math.fround(Math.fround(v)*100))} %`;
  return null;
}
export const textureGroups = [
  ['TEXTURE',['tex_on','tex_media','tex_mono']],
  ['GRAINS',['tex_time','tex_size','tex_shape','tex_pitch','tex_time_amt','tex_size_amt','tex_shape_amt','tex_pitch_amt']],
  ['SEED',['tex_density','tex_seed','tex_freeze']],
  ['MIX',['tex_feedback','tex_drywet','tex_reverb','tex_gain']]
];
