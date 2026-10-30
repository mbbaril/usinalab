// SPDX-License-Identifier: GPL-3.0-only
export function bindPopupMenu(panel,items,close){
  panel.addEventListener('keydown',e=>{
    if(e.key==='Tab'){close(true);return;}
    if(!['ArrowDown','ArrowUp','Home','End'].includes(e.key))return;
    e.preventDefault();const enabled=items.filter(b=>!b.disabled),index=enabled.findIndex(b=>b===e.target||b.contains?.(e.target));
    const next=e.key==='Home'?0:e.key==='End'?enabled.length-1:(index+(e.key==='ArrowDown'?1:-1)+enabled.length)%enabled.length;
    enabled[next]?.focus();
  });
}
