// SPDX-License-Identifier: GPL-3.0-only
const clamp=(n,a,b)=>Math.max(a,Math.min(b,n));
export class ScopeViewport{
  constructor(){this.available=5;this.reset();}
  reset(){this.seconds=5;this.gain=1;this.age=0;}
  constrain(available=this.available){this.available=Math.max(0,Math.min(5,available));this.seconds=clamp(this.seconds,.25,5);this.gain=clamp(this.gain,.25,8);this.age=clamp(this.age,0,Math.max(0,this.available-this.seconds));}
  navigate(initial,a,b){const x=(a.x+b.x)/2,dx=Math.abs(a.x-b.x),dy=Math.abs(a.y-b.y);this.seconds=initial.seconds/(initial.dx>.03?Math.max(.01,dx)/initial.dx:1);this.gain=initial.gain*(initial.dy>.07?Math.max(.01,dy)/initial.dy:1);this.constrain();this.age=initial.anchor-(1-x)*this.seconds;this.constrain();}
}
export function installScopeViewport({svg,view,paint}){
  const pointers=new Map();let initial=null;
  const point=e=>{const r=svg.getBoundingClientRect();return {x:(e.clientX-r.left)/r.width,y:(e.clientY-r.top)/r.height};};
  svg.style.touchAction='none';
  svg.onpointerdown=e=>{if(e.pointerType==='mouse'&&e.button!==0)return;e.preventDefault();pointers.set(e.pointerId,point(e));svg.setPointerCapture(e.pointerId);if(pointers.size===2){const[a,b]=[...pointers.values()];initial={seconds:view.seconds,gain:view.gain,anchor:view.age+(1-(a.x+b.x)/2)*view.seconds,dx:Math.abs(a.x-b.x),dy:Math.abs(a.y-b.y)};}};
  svg.onpointermove=e=>{if(!pointers.has(e.pointerId))return;e.preventDefault();pointers.set(e.pointerId,point(e));if(pointers.size===2&&initial){const[a,b]=[...pointers.values()];view.navigate(initial,a,b);paint();}};
  const end=e=>{if(!pointers.delete(e.pointerId))return;if(svg.hasPointerCapture(e.pointerId))svg.releasePointerCapture(e.pointerId);if(pointers.size<2)initial=null;};svg.onpointerup=svg.onpointercancel=svg.onlostpointercapture=end;
  return ()=>{const ids=[...pointers.keys()];pointers.clear();initial=null;for(const id of ids)if(svg.hasPointerCapture(id))svg.releasePointerCapture(id);};
}
