// SPDX-License-Identifier: GPL-3.0-only
export function snapshotPresentation({stored,current,capture,locked,name}){
 return {caption:stored?(current?'Aqui':'Gravado'):'Vazio',label:`${name}: ${stored?'gravado':'vazio'}. ${locked?(stored?'Recuperar instantâneo.':'Captura bloqueada.'):(capture?'Guardar as quatro macros.':stored?'Recuperar; segure para substituir.':'Toque para guardar as quatro macros.')}`};
}
export function motionPresentation({recording,armed,gliding,looping,hasLoop,locked}){
 if(recording)return{phase:'recording',text:'Gravando gesto'};
 if(armed)return{phase:'armed',text:'REC pronto · arraste o XY'};
 if(gliding)return{phase:'transition',text:'Transição'};
 if(looping)return{phase:'loop',text:'Gesto em loop'};
 return{phase:'idle',text:locked?'Captura bloqueada':hasLoop?'Gesto pronto · LOOP para tocar':'XY · mistura das quatro macros'};
}
export function installSnapshotPress({target,store,recall,stored,capturing,schedule=setTimeout,unschedule=clearTimeout}){
 let timer=null,pressed=null,long=false;
 const clear=()=>{if(timer!==null)unschedule(timer);timer=null;};
 const cancel=()=>{const previous=pressed;clear();pressed=null;long=false;if(previous&&target.hasPointerCapture?.(previous.id))target.releasePointerCapture(previous.id);};
 target.onpointerdown=e=>{if(e.button>0||pressed)return;pressed={id:e.pointerId,x:e.clientX,y:e.clientY};target.setPointerCapture?.(e.pointerId);long=false;timer=schedule(()=>{timer=null;if(pressed){long=true;store();}},550);};
 target.onpointermove=e=>{if(pressed?.id===e.pointerId&&Math.hypot(e.clientX-pressed.x,e.clientY-pressed.y)>8){clear();long=true;}};
 target.onpointerup=e=>{if(pressed?.id!==e.pointerId)return;clear();pressed=null;};target.onpointercancel=cancel;target.onlostpointercapture=e=>{if(pressed?.id===e.pointerId)cancel();};
 target.oncontextmenu=e=>{e.preventDefault();clear();if(!long)store();long=true;};
 target.onclick=()=>{if(long){long=false;return;}if(capturing()||!stored())store();else recall();};
 return cancel;
}
