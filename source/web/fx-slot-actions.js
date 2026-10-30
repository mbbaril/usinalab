// SPDX-License-Identifier: GPL-3.0-only
export function createFxSlotActions({el,slots,selected,param,exchange}){
  const root=el('details','fx-slot-actions'),summary=el('summary','','Organizar slots'),source=el('select'),destination=el('select'),move=el('button','','Mover para slot vazio'),swap=el('button','','Trocar slots');
  source.setAttribute('aria-label','Slot de origem');destination.setAttribute('aria-label','Slot de destino');
  for(const slot of slots){for(const select of [source,destination]){const o=el('option','',slot.label);o.value=slot.id;select.append(o);}}
  source.value=selected;destination.value=slots.find(s=>s.id!==selected&&!param(s.id))?.id||slots.find(s=>s.id!==selected)?.id||selected;
  const sync=()=>{const valid=source.value!==destination.value&&!!param(source.value);move.disabled=!valid||!!param(destination.value);swap.disabled=!valid||!param(destination.value);};
  source.onchange=destination.onchange=sync;
  move.onclick=()=>{sync();if(!move.disabled)exchange(source.value,destination.value,'move');};
  swap.onclick=()=>{sync();if(!swap.disabled)exchange(source.value,destination.value,'swap');};
  root.onkeydown=e=>{if(e.key==='Escape'){e.preventDefault();root.open=false;summary.focus();}};
  root.append(summary,source,destination,move,swap);sync();return root;
}
