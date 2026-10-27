// SPDX-License-Identifier: GPL-3.0-only
import {mountOperatorRows} from './operator-layout.js';
export const textureRows={
 TEXTURE:[['tex_on'],['tex_media'],['tex_mono']],
 GRAINS:[['tex_time','tex_size','tex_shape','tex_pitch'],['tex_time_amt','tex_size_amt','tex_shape_amt','tex_pitch_amt']],
 SEED:[['tex_density'],['tex_seed'],['tex_freeze']],
 MIX:[['tex_feedback','tex_drywet'],['tex_reverb','tex_gain']]
};
const categorical=new Set(['tex_on','tex_media','tex_mono','tex_seed','tex_freeze']);
export function renderTextureLayout({root,el,fields,field,section}){
 const layout=el('div','fx-texture-board');root.append(layout);
 for(const [name,rows]of Object.entries(textureRows)){
  const part=section(layout,name,[]);part.dataset.textureGroup=name;
  const controls=mountOperatorRows({root:part,el,prefix:'',rows,descriptor:id=>fields.find(d=>d.id===id),field,choices:categorical,resolve:d=>d});controls.classList.add('texture-native-layout');
 }return layout;
}
