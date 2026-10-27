// SPDX-License-Identifier: GPL-3.0-only
// A gesture acquires one native fader/macro; crossing neighbours cannot change their parameters.
export function installCabineGestures({grid,pads,mode,activate}) {
  const pointers=new Map(),owners=new Map();
  const nearest=(value,points)=>points.reduce((best,p,i)=>Math.abs(p-value)<Math.abs(points[best]-value)?i:best,0);
  grid.onpointerdown=e=>{const pad=e.target.closest('[data-pad]');if(!pad||!grid.contains(pad))return;const index=+pad.dataset.pad,row=Math.floor(index/8),col=index%8;
    if(row!==7&&mode!=='GESTOS')return;e.preventDefault();
    const group=row===7?Math.floor(col/4):col,key=row===7?`macro${group+1}`:col<4?`macro${col+1}`:`fader${col}`;
    if(owners.has(key))return;
    const points=row===7?Array.from({length:4},(_,i)=>{const r=pads[56+group*4+i].getBoundingClientRect();return r.left+r.width/2;}):Array.from({length:7},(_,i)=>{const r=pads[i*8+col].getBoundingClientRect();return r.top+r.height/2;});
    pointers.set(e.pointerId,{key,row,col,group,points,index});owners.set(key,e.pointerId);grid.setPointerCapture(e.pointerId);activate(index,false);
  };
  grid.onpointermove=e=>{const p=pointers.get(e.pointerId);if(!p)return;e.preventDefault();const index=p.row===7?56+p.group*4+nearest(e.clientX,p.points):nearest(e.clientY,p.points)*8+p.col;if(index===p.index)return;p.index=index;activate(index,true);};
  const end=e=>{const p=pointers.get(e.pointerId);if(!p)return;if(e.type==='pointerup')grid.onpointermove(e);pointers.delete(e.pointerId);owners.delete(p.key);};grid.onpointerup=grid.onpointercancel=grid.onlostpointercapture=end;
  pads.forEach((pad,index)=>{const drag=Math.floor(index/8)===7||mode==='GESTOS';if(drag)pad.style.touchAction='none';pad.onclick=e=>{if(drag&&e.detail!==0)return;activate(index,false);};});
  return ()=>{const ids=[...pointers.keys()];pointers.clear();owners.clear();for(const id of ids)if(grid.hasPointerCapture(id))grid.releasePointerCapture(id);};
}
