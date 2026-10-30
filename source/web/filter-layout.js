// SPDX-License-Identifier: GPL-3.0-only
import {mountOperatorRows} from './operator-layout.js';
import {filterParameter} from './synth-parameters.js';
// FilterPanel::resized: common row, topology row, EG2, and OUTPUT above LPG.
export const filterRows={
  common:[['filter','filter_type'],['cutoff','reso','flt_drive','eg2_int','key_low','key_high','key_center','flt_offset']],
  cascade:[['hp_cut','hp_peak','casc_fb','hp_eg2_int','casc_out',['casc_vca','casc_source','casc_hot']]],
  cmos:[['cmos_life']],
  envelope:[['eg2_atk','eg2_dec','eg2_sus','eg2_rel','eg2_curve'],['eg2_atk_curve','eg2_rel_curve',['eg2_mode','eg2_retrig']]],
  output:[['out_gain','width','pan'],['lofi','spread']],   // Stereo voice controls mirror the plugin OUTPUT group.
  lpg:[['lpg_mode','lpg_source'],['lpg_r_mode','lpg_r_source'],['lpg_decay','lpg_color','lpg_r_decay']],   // its sources, its right side
  filter2:[['f2_route','f2_type'],['f2_cutoff','f2_reso','f2_drive','f2_eg2_int','f2_key','f2_mix']]   // FILTER 2's page
};
export function mountFilterRows({root,el,rows,descriptor,field,state}){
  return mountOperatorRows({root,el,prefix:'',rows,descriptor,field,choices:new Set(['filter','filter_type','lofi','lpg_mode','spread','lpg_source','lpg_r_mode','lpg_r_source','f2_route','f2_type']),resolve:d=>filterParameter({...d,dim:state.dim&&/^eg2_(atk|dec|sus|rel|curve)$/.test(d.id)},state)});
}
