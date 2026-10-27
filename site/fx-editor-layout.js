// SPDX-License-Identifier: GPL-3.0-only
// Parameter rows from DelayPanel/VaporPanel/DistortPanel/RoomPanel::resized.
export const fxLayouts={
  2:{controls:[['rev_on']],body:['rev_amount','rev_color'],knobs:[]},
  3:{controls:[['dist_on','dist_mode']],knobs:[['dist_drive','dist_char','dist_sym','dist_tone','dist_mix','dist_out']]},
  5:{controls:[['vap_on','vap_mode','vap_quality'],['vap_trig','vap_freeze','vap_reverse']],knobs:[['vap_position','vap_size','vap_pitch','vap_density','vap_texture'],['vap_drywet','vap_spread','vap_feedback','vap_reverb']]},
  6:{controls:[['dly_on','dly_character','dly_routing'],['dly_sync','dly_freeze','TAP']],knobs:[['dly_time','dly_feedback','dly_lowcut','dly_highcut'],['dly_wow','dly_flutter','dly_mix','dly_out']]}
};
const inlineSwitches=new Set(['rev_on','dist_on','vap_on','vap_freeze','vap_reverse','dly_on','dly_freeze']);
const inlineChoices=new Set(['dist_mode','vap_mode','vap_quality','dly_character','dly_routing']);
const captions={rev_on:'ROOM',rev_amount:'AMOUNT',rev_color:'COLOR',dist_on:'DISTORT',dist_mode:'MODE',dist_drive:'DRIVE',dist_sym:'SYMMETRY',dist_tone:'TONE',dist_mix:'MIX',dist_out:'OUTPUT',vap_on:'VAPOR',vap_mode:'MODE',vap_quality:'QUALITY',vap_trig:'TRIG',vap_freeze:'FREEZE',vap_reverse:'REVERSE',dly_on:'DELAY',dly_character:'CHARACTER',dly_routing:'ROUTING',dly_sync:'SYNC',dly_freeze:'FREEZE',dly_time:'TIME',dly_feedback:'FEEDBACK',dly_lowcut:'LOW CUT',dly_highcut:'HIGH CUT',dly_wow:'WOW',dly_flutter:'FLUTTER',dly_mix:'MIX',dly_out:'OUTPUT'};
export function renderFxEditorLayout({root,el,kind,fields,field,tap}) {
  const plan=fxLayouts[kind];if(!plan)return false;
  const layout=el('div','fx-editor-layout');layout.dataset.fxKind=kind;root.append(layout);
  const append=(row,id)=>{
    if(id==='TAP'){if(tap)row.append(tap);return;}
    const d=fields.find(d=>d.id===id);if(d){const caption=d.uiCaption??captions[id]??d.name,item=field({...d,uiCaption:caption,compactToggle:true,toggleLabel:inlineSwitches.has(id)?caption:undefined});if(inlineSwitches.has(id)||inlineChoices.has(id))item.dataset.fxCaption='inline';row.append(item);}
  };
  plan.controls.forEach((ids,i)=>{const row=el('div','choice-controls fx-control-row');row.dataset.controlRow=i;ids.forEach(id=>append(row,id));layout.append(row);});
  if(plan.body){const row=el('div','deep-controls fx-room-body');plan.body.forEach(id=>append(row,id));layout.append(row);}
  plan.knobs.forEach((ids,i)=>{const row=el('div','deep-controls fx-knob-row');row.dataset.knobRow=i;row.dataset.columns=ids.length;row.style.setProperty('--fx-columns',ids.length);ids.forEach(id=>append(row,id));layout.append(row);});
  return true;
}
