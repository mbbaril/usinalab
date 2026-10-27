// SPDX-License-Identifier: GPL-3.0-only
const clamp=(n,a,b)=>Math.max(a,Math.min(b,n));
export class GraphViewport{
  constructor(width=300,height=72){this.fullWidth=width;this.fullHeight=height;this.reset();}
  reset(){this.x=0;this.y=0;this.width=this.fullWidth;this.height=this.fullHeight;}
  point(p){return {x:this.x+p.x*this.width,y:this.y+p.y*this.height};}
  navigate(initial,a,b){const mid={x:(a.x+b.x)/2,y:(a.y+b.y)/2};this.width=clamp(initial.width/(initial.dx>.03?Math.max(.01,Math.abs(a.x-b.x))/initial.dx:1),this.fullWidth/4,this.fullWidth);this.height=clamp(initial.height/(initial.dy>.07?Math.max(.01,Math.abs(a.y-b.y))/initial.dy:1),this.fullHeight/4,this.fullHeight);this.x=clamp(initial.anchor.x-mid.x*this.width,0,this.fullWidth-this.width);this.y=clamp(initial.anchor.y-mid.y*this.height,0,this.fullHeight-this.height);}
}
// A pending contact can become navigation. Once editing starts, extra fingers cannot take over that edit.
export function installEditableGraph({svg,view,beginEdit,moveEdit,paint}){
  const pointers=new Map();let mode='idle',edit=null,initial=null;
  const position=e=>{const r=svg.getBoundingClientRect();return {x:(e.clientX-r.left)/r.width,y:(e.clientY-r.top)/r.height,clientX:e.clientX,clientY:e.clientY};};
  const contacts=()=>svg.dataset.contacts=pointers.size;
  svg.onpointerdown=e=>{if(e.pointerType==='mouse'&&e.button!==0)return;e.preventDefault();const p=position(e);pointers.set(e.pointerId,p);contacts();svg.setPointerCapture(e.pointerId);
    if(pointers.size===1){mode='pending';edit={id:e.pointerId,p,data:beginEdit(view.point(p),e)};}
    else if(pointers.size===2&&(mode==='pending'||mode==='navigate')){mode='navigate';edit=null;const[a,b]=[...pointers.values()];initial={width:view.width,height:view.height,anchor:view.point({x:(a.x+b.x)/2,y:(a.y+b.y)/2}),dx:Math.abs(a.x-b.x),dy:Math.abs(a.y-b.y)};}else if(pointers.size>2){mode='wait';edit=null;initial=null;}
  };
  svg.onpointermove=e=>{if(!pointers.has(e.pointerId))return;e.preventDefault();const p=position(e);pointers.set(e.pointerId,p);
    if(mode==='navigate'){if(pointers.size===2){const[a,b]=[...pointers.values()];view.navigate(initial,a,b);paint();}return;}
    if(edit?.id!==e.pointerId)return;if(mode==='pending'&&edit.data!==null&&Math.hypot(p.clientX-edit.p.clientX,p.clientY-edit.p.clientY)>=6)mode='edit';if(mode==='edit')moveEdit(edit.data,view.point(p));
  };
  const end=e=>{if(!pointers.has(e.pointerId)||e.type==='lostpointercapture'&&svg.hasPointerCapture(e.pointerId))return;if(e.type==='pointerup')svg.onpointermove(e);pointers.delete(e.pointerId);contacts();if(edit?.id===e.pointerId){edit=null;mode='wait';}if(svg.hasPointerCapture(e.pointerId))svg.releasePointerCapture(e.pointerId);if(!pointers.size){mode='idle';initial=null;}};svg.onpointerup=svg.onpointercancel=svg.onlostpointercapture=end;
  return ()=>{const ids=[...pointers.keys()];pointers.clear();contacts();mode='idle';edit=null;initial=null;for(const id of ids)if(svg.hasPointerCapture(id))svg.releasePointerCapture(id);};
}
