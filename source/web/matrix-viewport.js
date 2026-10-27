// SPDX-License-Identifier: GPL-3.0-only
const clamp=(n,a,b)=>Math.max(a,Math.min(b,n));
export function matrixTransform(initial,a,b,width,height){
  const mid={x:(a.x+b.x)/2,y:(a.y+b.y)/2},distance=Math.hypot(a.x-b.x,a.y-b.y),scale=clamp(initial.scale*distance/Math.max(1,initial.distance),1,3);
  return {scale,x:clamp(mid.x-initial.anchor.x*scale,width*(1-scale),0),y:clamp(mid.y-initial.anchor.y*scale,height*(1-scale),0)};
}
// Only routing cells participate. Macro strips keep their existing immediate, independent touch ownership.
export function installMatrixViewport({frame,grid,headers,labels=[],changed=()=>{}}){
  let transform={scale:1,x:0,y:0},initial=null,navigating=false;const pointers=new Map(),consumed=new Set(),performanceTouches=new Set();
  const paint=()=>{grid.style.transform=`translate(${transform.x}px,${transform.y}px) scale(${transform.scale})`;headers.style.width=`${transform.scale*100}%`;headers.style.transform=`translateX(${transform.x}px)`;frame.dataset.zoom=transform.scale.toFixed(3);const h=frame.getBoundingClientRect().height;labels.forEach((label,i)=>{const y=(i+.5)/8*h*transform.scale+transform.y;label.style.top=`${y}px`;label.hidden=transform.scale<1.02||y<0||y>h;});changed(transform.scale);};
  const position=e=>{const r=frame.getBoundingClientRect();return {x:e.clientX-r.left,y:e.clientY-r.top};};
  const down=e=>{consumed.delete(e.pointerId);if(e.pointerType==='mouse'&&e.button!==0)return;const pad=e.target.closest('[data-pad]');if(!pad)return;if(Math.floor(+pad.dataset.pad/8)>=7){if(navigating){consumed.add(e.pointerId);e.preventDefault();e.stopPropagation();}else performanceTouches.add(e.pointerId);return;}if(performanceTouches.size)return;pointers.set(e.pointerId,position(e));if(pointers.size===2){e.preventDefault();e.stopPropagation();navigating=true;const[a,b]=[...pointers.values()],mid={x:(a.x+b.x)/2,y:(a.y+b.y)/2};initial={...transform,distance:Math.hypot(a.x-b.x,a.y-b.y),anchor:{x:(mid.x-transform.x)/transform.scale,y:(mid.y-transform.y)/transform.scale}};for(const id of pointers.keys()){consumed.add(id);frame.setPointerCapture(id);}}};
  const move=e=>{if(!pointers.has(e.pointerId))return;pointers.set(e.pointerId,position(e));if(!navigating)return;e.preventDefault();e.stopPropagation();if(pointers.size===2){const[a,b]=[...pointers.values()],r=frame.getBoundingClientRect();transform=matrixTransform(initial,a,b,r.width,r.height);paint();}};
  const end=e=>{performanceTouches.delete(e.pointerId);if(!pointers.has(e.pointerId))return;pointers.delete(e.pointerId);if(navigating){e.preventDefault();e.stopPropagation();if(frame.hasPointerCapture(e.pointerId))frame.releasePointerCapture(e.pointerId);if(!pointers.size){navigating=false;initial=null;}}};
  const click=e=>{if(e.detail!==0&&(consumed.has(e.pointerId)||navigating)){e.preventDefault();e.stopPropagation();}};
  frame.addEventListener('pointerdown',down,true);frame.addEventListener('pointermove',move,true);for(const kind of ['pointerup','pointercancel','lostpointercapture'])frame.addEventListener(kind,end,true);frame.addEventListener('click',click,true);
  const cancel=()=>{const ids=[...pointers.keys()];pointers.clear();performanceTouches.clear();initial=null;navigating=false;for(const id of ids)if(frame.hasPointerCapture(id))frame.releasePointerCapture(id);};
  paint();return {cancel,reset(){cancel();transform={scale:1,x:0,y:0};paint();}};
}
