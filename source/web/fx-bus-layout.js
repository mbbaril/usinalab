// SPDX-License-Identifier: GPL-3.0-only
import {fxValueFormatter} from './fx-values.js';
export const fxBusControls={
 'ROUTING':['fx_on','fx_route'],
 'FX A':['fx_a_on','fx_a_mix'],
 'FX B':['fx_b_on','fx_b_mix'],
 'AUX':['aux_pre','aux_on','aux_send','aux_return'],
 'LOOP':['loop_fx_on','feedback','fb_time']
};
const captions={fx_on:'EFFECTS',fx_route:'ROUTING',fx_a_on:'FX A ON',fx_b_on:'FX B ON',aux_on:'AUX ON',loop_fx_on:'LOOP ON',aux_pre:'AUX PRE / POST',fx_a_mix:'MIX',fx_b_mix:'MIX',aux_send:'SEND',aux_return:'RETURN',feedback:'FEEDBACK',fb_time:'TIME'};
export function fxBusParameter(d,param){return {...d,uiCaption:captions[d.id]||d.name,text:fxValueFormatter(d.id)||undefined,...(d.id==='fx_route'?{choices:['A > B','A // B','B > A']}:{...(['fx_a_on','fx_b_on','aux_on','loop_fx_on'].includes(d.id)?{toggleLabel:'ON'}:d.id==='fx_on'?{toggleLabel:'EFFECTS'}:d.id==='aux_pre'?{toggleLabel:()=>param('aux_pre')?'PRE':'POST'}:{})})};}
export function fxBusFields(descriptors,group,param){return (fxBusControls[group]||[]).map(id=>descriptors.find(d=>d.id===id)).filter(Boolean).map(d=>fxBusParameter(d,param));}
