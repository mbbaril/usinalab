// SPDX-License-Identifier: GPL-3.0-only
import {sourceColor} from './controls.js';
import {tactilePulse} from './haptics.js';
import {installVerticalGesture} from './vertical-gesture.js';
export function mountModulationTray({root,destination,title,metadata,state,send,el,changed}) {
  const tray=el('div','mod-tray');tray.setAttribute('aria-label',`Modulações de ${title}`);root.append(tray);
  const items=new Map();
  const indexOf=route=>state.routes.indexOf(route);
  function amount(route,value) {const index=indexOf(route);if(index<0||!Number.isFinite(value))return;const previous=route.amount;route.amount=Math.min(1,Math.max(-1,value));if(previous*route.amount<0)tactilePulse();send({type:'route',index,...route});refresh();root.modFeedbackSync?.();}
  function remove(route){const index=indexOf(route);if(index<0)return;state.routes.splice(index,1);send({type:'route',index,source:0});tactilePulse();changed();}
  function create(route) {
    const source=metadata().sources.find(s=>s.index===route.source),item=el('div','mod-route'),label=el('span','mod-source',source?.name||'Fonte'),dial=el('div','mod-mini'),value=el('output'),close=el('button','mod-remove','×');
    item.style.setProperty('--route-color',sourceColor(source?.id));dial.tabIndex=0;dial.setAttribute('role','slider');dial.setAttribute('aria-label',`${source?.name} → ${title}: profundidade`);dial.setAttribute('aria-valuemin',-1);dial.setAttribute('aria-valuemax',1);dial.dataset.source=route.source;
    const cancel=installVerticalGesture({target:dial,get:()=>route.amount,set:v=>amount(route,v),sensitivity:150,stopPropagation:true});
    dial.onkeydown=e=>{if(['ArrowUp','ArrowDown','ArrowLeft','ArrowRight','Home','End'].includes(e.key)){e.preventDefault();amount(route,e.key==='Home'?-1:e.key==='End'?1:route.amount+(['ArrowUp','ArrowRight'].includes(e.key)?.01:-.01));}};
    dial.ondblclick=e=>{e.preventDefault();remove(route);};dial.oncontextmenu=e=>{e.preventDefault();remove(route);};
    close.type='button';close.setAttribute('aria-label',`Remover ${source?.name} de ${title}`);close.onclick=e=>{e.preventDefault();remove(route);};
    item.append(label,dial,value,close);
    return {item,dial,value,route,source:route.source,cancel};
  }
  function refresh() {
    const mine=state.routes.filter(r=>r.destination===destination);
    for(const [route,item] of items)if(!mine.includes(route)||item.source!==route.source){item.cancel();item.item.remove();items.delete(route);}
    for(const route of mine){let item=items.get(route);if(!item){item=create(route);items.set(route,item);tray.append(item.item);}const a=route.amount;item.dial.setAttribute('aria-valuenow',a);item.dial.setAttribute('aria-valuetext',`${a>0?'+':''}${Math.round(a*100)} por cento`);item.value.textContent=`${a>0?'+':''}${Math.round(a*100)}%`;item.dial.style.setProperty('--mini-angle',`${a*306}deg`);item.dial.style.setProperty('--mini-low',`${a<0?360+a*306:0}deg`);item.dial.style.setProperty('--mini-high',`${a>=0?a*306:360}deg`);}
    tray.hidden=!mine.length;root.classList.toggle('has-modulation',!!mine.length);
  }
  refresh();return {refresh,cancelGestures(){for(const item of items.values())item.cancel();},focus(source){refresh();items.get(state.routes.find(r=>r.source===source&&r.destination===destination))?.dial.focus({preventScroll:true});}};
}
