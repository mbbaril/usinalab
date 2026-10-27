// SPDX-License-Identifier: GPL-3.0-only
import {sourceColor} from './controls.js';
import {installVerticalGesture} from './vertical-gesture.js';
export const routeBar=amount=>({left:50+Math.min(0,amount)*50,width:Math.abs(amount)*50});
export function mountRouteList({root,el,state,metadata,send,changed}){
 const list=el('div','native-route-list'),empty=el('p','route-empty','Ainda sem rotas. Use + no controle e escolha uma fonte.');root.append(list,empty);const items=new Map();
 function amount(route,value){const index=state.routes.indexOf(route);if(index<0||!Number.isFinite(value))return;route.amount=Math.min(1,Math.max(-1,value));send({type:'route',index,...route});refresh();changed();}
 function create(route){
  const row=el('div','native-route-row'),control=el('div','route-amount'),name=el('span','route-name'),bar=el('span','route-amount-bar'),fill=el('span'),remove=el('button','route-delete','×');control.tabIndex=0;control.setAttribute('role','slider');control.setAttribute('aria-valuemin',-1);control.setAttribute('aria-valuemax',1);bar.append(fill);control.append(name,bar);row.append(control,remove);
  const cancel=installVerticalGesture({target:control,get:()=>route.amount,set:v=>amount(route,v),sensitivity:120,axis:'x',stopPropagation:true});
  control.onkeydown=e=>{if(['ArrowRight','ArrowLeft','ArrowUp','ArrowDown','Home','End'].includes(e.key)){e.preventDefault();amount(route,e.key==='Home'?-1:e.key==='End'?1:route.amount+(['ArrowUp','ArrowRight'].includes(e.key)?.01:-.01)*(e.shiftKey?.1:1));}};
  remove.type='button';remove.onclick=()=>{const index=state.routes.indexOf(route);if(index<0)return;cancel();state.routes.splice(index,1);send({type:'route',index,source:0});refresh();changed();};
  return {row,control,name,bar,fill,remove,cancel};
 }
 function refresh(){
  for(const[route,item]of items)if(!state.routes.includes(route)){item.cancel();item.row.remove();items.delete(route);}
  const meta=metadata();for(const route of state.routes){let item=items.get(route);if(!item){item=create(route);items.set(route,item);list.append(item.row);}const source=meta.sources.find(s=>s.index===route.source),dest=meta.destinations.find(d=>d.index===route.destination),label=`${source?.name||'—'} > ${dest?.name||'—'}`,value=route.amount,b=routeBar(value);item.name.textContent=label;item.name.title=label;item.row.style.setProperty('--route-color',sourceColor(source?.id));item.control.setAttribute('aria-label',`${label}: profundidade`);item.control.setAttribute('aria-valuenow',value);item.control.setAttribute('aria-valuetext',`${value>0?'+':''}${Math.round(value*100)}%`);item.control.title=`${label}: ${Math.round(value*100)}%`;item.fill.style.left=`${b.left}%`;item.fill.style.width=`${b.width}%`;item.remove.setAttribute('aria-label',`Remover ${label}`);}
  empty.hidden=!!state.routes.length;root.dataset.routeCount=state.routes.length;root.dispatchEvent(new Event('routecount'));
 }
 root.refreshRoutes=refresh;root.cancelRouteGestures=()=>{for(const item of items.values())item.cancel();};refresh();return {refresh};
}
