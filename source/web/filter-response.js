// SPDX-License-Identifier: GPL-3.0-only
import {createResponseCurve} from './response-curve.js';
const clamp=(v,lo,hi)=>Math.max(lo,Math.min(hi,v));
export const cutoffHz=value=>6.489*(23680/6.489)**(clamp(value,0,1000)/1000);
export function filterPoint(x,y,rect){return {cutoff:Math.round(clamp((x-rect.left)/rect.width,0,1)*1000),reso:Math.round((1-clamp((y-rect.top)/rect.height,0,1))*100)};}
const svg=(tag,attrs={})=>{const n=document.createElementNS('http://www.w3.org/2000/svg',tag);for(const [k,v] of Object.entries(attrs))n.setAttribute(k,v);return n;};
// One worker, one computation at a time, with only the latest pending request retained.
let worker,busy=false,pending=null,serial=0,callbacks=new Map();
function submit(params,done,owner){const id=++serial;if(pending)callbacks.delete(pending.id);callbacks.set(id,{done,owner});pending={id,params};pump();return id;}
function pump(){if(busy||!pending)return;try{if(!worker){worker=new Worker(new URL('./filter-response-worker.js',import.meta.url),{type:'module'});worker.onmessage=({data})=>{busy=false;callbacks.get(data.id)?.done(data);callbacks.delete(data.id);pump();};worker.onerror=()=>{busy=false;worker.terminate();worker=null;pending=null;for(const job of callbacks.values())job.done({status:2});callbacks.clear();};}busy=true;worker.postMessage(pending);pending=null;}catch{busy=false;pending=null;for(const job of callbacks.values())job.done({status:2});callbacks.clear();}}
export function filterResponseView({param,setParam,rate=()=>({rate:192000,firmware:false})}){
  const root=document.createElement('div'),head=document.createElement('div'),readout=document.createElement('output'),note=document.createElement('span');root.className='filter-response';root.dataset.readback='';head.className='filter-response-head';head.append(readout,note);root.append(head);
  const art=svg('svg',{viewBox:'0 0 360 170',preserveAspectRatio:'none',class:'filter-response-plot',role:'group','aria-label':'Resposta nominal do filtro. Horizontal: cutoff. Vertical: ressonância.'}),grid=svg('g',{class:'filter-response-grid'}),curve=svg('path',{class:'filter-response-curve'}),cut=svg('line',{class:'filter-response-cut',y1:0,y2:170}),hp=svg('line',{class:'filter-response-hp',y1:0,y2:170}),handle=svg('circle',{class:'filter-response-handle',r:6});
  for(const db of [48,24,0,-24,-48]){const y=(48-db)/120*170;grid.append(svg('line',{x1:0,x2:360,y1:y,y2:y}));const label=svg('text',{x:3,y:Math.max(10,y-3)});label.textContent=`${db} dB`;grid.append(label);}
  for(const hz of [10,100,1000,10000]){const x=Math.log(hz/6.489)/Math.log(23680/6.489)*360;grid.append(svg('line',{x1:x,x2:x,y1:0,y2:170}));const label=svg('text',{x:x+3,y:165});label.textContent=hz>=1000?`${hz/1000}k`:hz;grid.append(label);}
  art.setAttribute('tabindex','0');art.append(grid,curve,hp,cut,handle);root.append(art);
  const help=document.createElement('span');help.className='filter-response-help';help.textContent='↔ CUTOFF · ↕ RESO — pequeno sinal, sem EG/KEY/modulação';root.append(help);
  const animation=createResponseCurve({write:path=>curve.setAttribute('d',path)});
  let key='',family='',timer=null,drag=null,lastSubmit=-Infinity,wanted=null,disposed=false;
  function cancel(){if(drag!==null){const id=drag;drag=null;if(art.hasPointerCapture(id))art.releasePointerCapture(id);}root.dataset.contacts='0';}
  function draw(){const on=!!param('filter'),v=param('cutoff'),r=param('reso'),x=v*.36;root.classList.toggle('inactive-param',!on);art.setAttribute('aria-disabled',!on);cut.setAttribute('x1',x);cut.setAttribute('x2',x);handle.setAttribute('cx',x);handle.setAttribute('cy',(100-r)*1.7);hp.style.display=param('filter_type')===11?'':'none';hp.setAttribute('x1',param('hp_cut')*.36);hp.setAttribute('x2',param('hp_cut')*.36);readout.textContent=`CUTOFF ${cutoffHz(v)<1000?cutoffHz(v).toFixed(1)+' Hz':(cutoffHz(v)/1000).toFixed(2)+' kHz'} · RESO ${r} %`;}
  function requestLatest(){timer=null;if(disposed||!wanted)return;const request=wanted;wanted=null;lastSubmit=performance.now();let id;id=submit(request.params,data=>{if(disposed||!root.isConnected||family!==request.family||!param('filter'))return;
      const current=key===request.key;root.dataset.responseStatus=current?String(data.status):'pending';
      note.textContent=current?(data.status===0?'PEQUENO SINAL':data.status===1?'RESPOSTA NÃO ESTABILIZADA':'MEDIÇÃO INDISPONÍVEL'):'ATUALIZANDO';
      if(data.status===0&&data.values){animation.set(data.values);root.dataset.measuredCutoff=request.params[1];root.dataset.measuredReso=request.params[2];root.dataset.responseRevision=String(id);}
      else if(current)animation.clear();
    },root);}
  root.sync=()=>{if(disposed)return;draw();const sample=rate(),params=[param('filter_type'),param('cutoff'),param('reso'),param('flt_drive'),param('hp_cut'),param('hp_peak'),param('casc_fb'),param('casc_hot'),param('cmos_life'),sample.rate,+sample.firmware],next=JSON.stringify([!!param('filter'),...params]);if(next===key)return;key=next;
    const nextFamily=JSON.stringify([!!param('filter'),params[0],sample.rate,+sample.firmware]);if(nextFamily!==family){family=nextFamily;animation.clear();}
    root.dataset.responseStatus='pending';if(!param('filter')){clearTimeout(timer);timer=null;wanted=null;note.textContent='BYPASS';animation.clear('M0 68H360');root.dataset.responseStatus='bypass';cancel();return;}
    note.textContent='ATUALIZANDO';wanted={params,key:next,family};if(timer===null)timer=setTimeout(requestLatest,Math.max(0,125-(performance.now()-lastSubmit)));
  };
  function move(e){if(drag!==e.pointerId)return;const r=art.getBoundingClientRect();if(!r.width||!r.height)return;const point=filterPoint(e.clientX,e.clientY,r);setParam('cutoff',point.cutoff);setParam('reso',point.reso);root.sync();}
  art.onpointerdown=e=>{if(!param('filter')||drag!==null||e.button>0)return;e.preventDefault();art.focus?.({preventScroll:true});drag=e.pointerId;root.dataset.contacts='1';art.setPointerCapture(e.pointerId);move(e);};art.onpointermove=move;
  art.onpointerup=e=>{if(drag!==e.pointerId)return;move(e);cancel();};art.onpointercancel=art.onlostpointercapture=e=>{if(drag===e.pointerId&&!(e.type==='lostpointercapture'&&art.hasPointerCapture(e.pointerId)))cancel();};
  root.cancelGraphGestures=cancel;root.disposeFilterResponse=()=>{disposed=true;cancel();clearTimeout(timer);animation.stop();for(const[id,job]of callbacks)if(job.owner===root){callbacks.delete(id);if(pending?.id===id)pending=null;}if(!callbacks.size){worker?.terminate();worker=null;pending=null;busy=false;}};root.sync();return root;
}
