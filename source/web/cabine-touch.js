// SPDX-License-Identifier: GPL-3.0-only
import {installVerticalGesture} from './vertical-gesture.js';
export const boundControl=(value,min=0,max=1)=>Number.isFinite(value)?Math.min(max,Math.max(min,value)):null;
export function bindPerformanceControl({target,get,set,min=0,max=1,sensitivity=220}){
  const apply=value=>{const v=boundControl(value,min,max);if(v!==null)set(v);};
  const cancel=installVerticalGesture({target,get,set:apply,sensitivity});
  target.tabIndex=0;target.setAttribute('role','slider');target.setAttribute('aria-valuemin',min);target.setAttribute('aria-valuemax',max);
  target.onkeydown=e=>{if(!['ArrowUp','ArrowRight','ArrowDown','ArrowLeft','Home','End'].includes(e.key))return;e.preventDefault();apply(e.key==='Home'?min:e.key==='End'?max:get()+(['ArrowUp','ArrowRight'].includes(e.key)?1:-1)*(max-min)*.01*(e.shiftKey?.1:1));};
  return cancel;
}
export function createRouteInspector({el,selected,update}){
  const root=el('section','cabine-route-inspector'),name=el('p','cabine-route-name','Toque uma ligação para ajustar sua quantidade.'),controls=el('div','cabine-depth-controls'),dial=el('div','cabine-depth-dial'),readout=el('output'),zero=el('button','play-toggle','ZERO');
  dial.setAttribute('aria-label','Quantidade da modulação');dial.title='Arraste para cima/baixo; Shift para ajuste fino.';
  const set=value=>{if(selected())update(value);};
  const cancel=bindPerformanceControl({target:dial,get:()=>selected()?.amount??0,set,min:-1,max:1,sensitivity:150});
  zero.onclick=()=>set(0);controls.append(dial,readout,zero);root.append(el('span','play-group-label','QUANTIDADE'),name,controls);
  function refresh(){const route=selected();controls.hidden=!route;name.textContent=route?.name||'Toque uma ligação para ajustar sua quantidade.';if(!route)return;const value=route.amount,text=`${value>0?'+':''}${Math.round(value*100)}%`;readout.textContent=text;dial.setAttribute('aria-valuenow',value);dial.setAttribute('aria-valuetext',text);dial.style.setProperty('--turn',`${value*135}deg`);root.style.setProperty('--route-color',route.color);}
  refresh();return{root,refresh,cancel};
}
export function renderGestureStrips({root,el,groups,names,normalized,setNormalized,readout,pulse}){
  const surface=el('section','cabine-gesture-surface'),strips=el('div','cabine-gesture-strips'),shortcuts=el('div','cabine-gesture-shortcuts'),items=[],cancels=[];
  surface.setAttribute('aria-label','GESTOS contínuos');
  groups.forEach((ids,i)=>{
    const field=el('div','cabine-gesture-field'),label=el('span','',names[i]),strip=el('div',`cabine-gesture-strip${i<4?' bipolar':''}`),fill=el('span','cabine-gesture-fill'),value=el('output');strip.append(fill);strip.setAttribute('aria-label',names[i]);field.style.setProperty('--gesture-color',['#ff5ca8','#ff5ca8','#ff5ca8','#ff5ca8','#c2f000','#25e0c4','#9d4edd','#3fa2ff'][i]);field.append(label,strip,value);strips.append(field);items.push({ids,strip,fill,value,bipolar:i<4});
    let lastBand=Math.round(normalized(ids[0])*4);
    cancels.push(bindPerformanceControl({target:strip,get:()=>normalized(ids[0]),set:v=>{ids.forEach(id=>setNormalized(id,v));const band=Math.round(v*4);if(band!==lastBand){pulse();lastBand=band;}refresh();}}));
  });
  for(let group=0;group<2;group++){const row=el('div','cabine-gesture-macro-row');row.append(el('span','',`MACRO ${group+1}`));for(let k=1;k<=4;k++){const value=k/4,b=el('button','play-toggle',`${k*25}%`);b.setAttribute('aria-label',`Macro ${group+1} ${k*25} por cento`);b.onclick=()=>{const id=`macro${group+1}`;setNormalized(id,Math.abs(normalized(id)-value)<.01?0:value);pulse();refresh();};b.dataset.gestureMacro=group;b.dataset.value=value;row.append(b);}shortcuts.append(row);}
  function refresh(){items.forEach(({ids,strip,fill,value,bipolar})=>{const v=normalized(ids[0]);fill.style.height=`${(bipolar?Math.abs(v-.5):v)*100}%`;if(bipolar)fill.style.bottom=v>=.5?'50%':`${v*100}%`;strip.setAttribute('aria-valuenow',v);const text=readout(ids);strip.setAttribute('aria-valuetext',text);value.textContent=text;});shortcuts.querySelectorAll('[data-gesture-macro]').forEach(b=>b.setAttribute('aria-pressed',String(normalized(`macro${+b.dataset.gestureMacro+1}`)>=+b.dataset.value-.001)));}
  surface.append(strips,shortcuts);root.append(surface);refresh();return{refresh,cancel(){cancels.forEach(cancel=>cancel());}};
}

