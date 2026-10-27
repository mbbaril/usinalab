// SPDX-License-Identifier: GPL-3.0-only
// Captured relative editing shared by parameter knobs and bipolar modulation amounts.
export function installVerticalGesture({target,get,set,sensitivity=220,stopPropagation=false,axis='y'}){
  let drag=null;
  const coordinate=e=>axis==='x'?-e.clientX:e.clientY;
  const cancel=()=>{const previous=drag;drag=null;target.dataset.dragging='false';if(previous&&target.hasPointerCapture(previous.id))target.releasePointerCapture(previous.id);};
  target.onpointerdown=e=>{if(drag||e.button>0)return;e.preventDefault();if(stopPropagation)e.stopPropagation();drag={id:e.pointerId,y:coordinate(e),value:get()};target.dataset.dragging='true';target.setPointerCapture(e.pointerId);};
  target.onpointermove=e=>{if(drag?.id!==e.pointerId)return;e.preventDefault();set(drag.value+(drag.y-coordinate(e))/(sensitivity*(e.shiftKey?5:1)));};
  const end=e=>{if(drag?.id!==e.pointerId||e.type==='lostpointercapture'&&target.hasPointerCapture(e.pointerId))return;if(e.type==='pointerup')target.onpointermove(e);cancel();};
  target.onpointerup=target.onpointercancel=target.onlostpointercapture=end;return cancel;
}
