// SPDX-License-Identifier: GPL-3.0-only
import {createTapControl} from './tap-control.js';
export const lfoShapes=[['SINE',4],['TRI',0],['SQR',3],['SAW DN',1],['SAW UP',2],['S&H',5]];
export function mountLfoActions({root,el,index,param,setParam,tap,readback,clock,stateRoot=root}) {
  const prefix=`lfo${index+1}_`,toolbar=el('div','lfo-actions');
  const buttons=lfoShapes.map(([name,value])=>{const b=el('button','lfo-shape',name);b.onclick=()=>setParam(prefix+'wave',value);b.setAttribute('aria-label',`LFO ${index+1}: ${name}`);toolbar.append(b);return b;});
  const tapButton=createTapControl({el,tap:time=>tap(index,time),readback:()=>readback(index),clock,label:`TAP LFO ${index+1}`});toolbar.append(tapButton);
  toolbar.dataset.readback='';toolbar.sync=()=>{
    buttons.forEach((b,i)=>b.setAttribute('aria-pressed',param(prefix+'wave')===lfoShapes[i][1]));
    for(const [id,dim]of [['speed',!!param(prefix+'sync')],['fade',!param(prefix+'key_sync')],['wave_b',!param(prefix+'morph')]])stateRoot.querySelector(`[data-param="${prefix+id}"]`)?.classList.toggle('inactive-param',dim);
    tapButton.sync();
  };
  root.append(toolbar);toolbar.sync();return toolbar;
}
