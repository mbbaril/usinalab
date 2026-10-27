// SPDX-License-Identifier: GPL-3.0-only
import {sourceColor} from './controls.js';
import {sourceBipolar} from './source-scope.js';
const clamp=(x,a=0,b=1)=>Math.max(a,Math.min(b,x));
// Knob::refreshMod: all reaches are summed in parameter units, then converted to knob travel.
export function modulationTravel(d,base,routes,sources,values){
  const span=d.max-d.min,skew=/^op\d_fixed_hz$/.test(d.id)?.25:d.skew||1,prop=v=>clamp((v-d.min)/span)**skew;
  let low=0,high=0,live=0,known=true,first=null,count=0;
  for(const route of routes){const source=sources.find(s=>s.index===route.source);if(!source||!Number.isFinite(route.amount))continue;first??=source;count++;const reach=Math.fround(route.amount)*span;
    if(sourceBipolar(source.id)){low-=Math.abs(reach);high+=Math.abs(reach);}else if(reach<0)low+=reach;else high+=reach;
    const value=values?.[route.source];if(!Number.isFinite(value))known=false;else live+=reach*Math.fround(value);
  }
  return {count,low:prop(base+low),high:prop(base+high),live:known?prop(base+live):null,color:sourceColor(first?.id)};
}
const polar=(p,r)=>{const a=(-135+270*p)*Math.PI/180;return [30+Math.sin(a)*r,30-Math.cos(a)*r];};
export function modulationArc(low,high){if(high-low<=.001)return '';const a=polar(low,22),b=polar(high,22);return `M${a.join(' ')}A22 22 0 ${high-low>2/3?1:0} 1 ${b.join(' ')}`;}
const svg=(tag,attrs)=>{const n=document.createElementNS('http://www.w3.org/2000/svg',tag);for(const[k,v]of Object.entries(attrs))n.setAttribute(k,v);return n;};
export function createModulationFeedback({metadata,state,param,send,ready}){
  let nodes=[],token=0,key='',queued=false,suspended=false;const values=new Float32Array(128);values.fill(NaN);
  const observer=new IntersectionObserver(()=>schedule());
  function visible(n){const r=n.dial.getBoundingClientRect();return n.root.isConnected&&r.width>0&&r.height>0&&r.bottom>0&&r.top<innerHeight&&r.right>0&&r.left<innerWidth;}
  function paint(n){if(n.source!==undefined){const value=values[n.source],live=ready()&&!suspended&&Number.isFinite(value);n.root.dataset.live=live?clamp(value):'';n.root.classList.toggle('no-reading',!live);n.fill.style.width=live?`${clamp(value)*100}%`:'0%';if(live)n.root.setAttribute('aria-valuenow',clamp(value));else n.root.removeAttribute('aria-valuenow');n.root.setAttribute('aria-valuetext',live?`${Math.round(clamp(value)*100)}%`:'Sem leitura');return;}const routes=state.routes.filter(r=>r.destination===n.destination),v=modulationTravel(n.d,param(n.d.id),routes,metadata()?.sources||[],values);n.art.style.display=v.count?'':'none';if(!v.count)return;n.art.style.setProperty('--feedback-color',v.color);n.arc.setAttribute('d',modulationArc(v.low,v.high));n.art.dataset.low=v.low;n.art.dataset.high=v.high;
    const live=ready()&&!suspended&&v.live!==null;n.dot.style.display=live?'':'none';n.art.dataset.live=live?v.live:'';if(live){const p=polar(v.live,27);n.dot.setAttribute('cx',p[0]);n.dot.setAttribute('cy',p[1]);}
  }
  function update(force=false){queued=false;nodes=nodes.filter(n=>n.root.isConnected);const active=nodes.filter(visible),ids=[...new Set(active.flatMap(n=>n.source!==undefined?[n.source]:state.routes.filter(r=>r.destination===n.destination).map(r=>r.source)))].sort((a,b)=>a-b),next=JSON.stringify(!ready()||suspended?[]:ids);
    if(force||next!==key){key=next;values.fill(NaN);send({type:'modSubscribe',sources:JSON.parse(next),token:++token});}for(const n of active)paint(n);
  }
  function schedule(){if(!queued){queued=true;requestAnimationFrame(()=>update());}}
  document.addEventListener('scroll',schedule,true);window.addEventListener('resize',schedule);
  return {
    mount(root,d,destination){const dial=root.querySelector('.dial');if(!dial)return;const art=svg('svg',{viewBox:'0 0 60 60',class:'mod-feedback','aria-hidden':'true'}),arc=svg('path',{class:'mod-feedback-range'}),dot=svg('circle',{class:'mod-feedback-dot',r:2.5});art.append(arc,dot);dial.append(art);const n={root,d,destination,dial,art,arc,dot};nodes.push(n);observer.observe(dial);root.modFeedbackSync=()=>{paint(n);schedule();};paint(n);schedule();},
    mountSource(root,source){const fill=document.createElement('span');fill.className='controller-meter-fill';root.append(fill);root.setAttribute('role','meter');root.setAttribute('aria-label',source.name);root.setAttribute('aria-valuemin',0);root.setAttribute('aria-valuemax',1);root.style.setProperty('--source-color',sourceColor(source.id));const n={root,dial:root,source:source.index,fill};nodes.push(n);observer.observe(root);paint(n);schedule();},
    refresh:schedule,
    clear(){observer.disconnect();nodes=[];values.fill(NaN);key='';send({type:'modSubscribe',sources:[],token:++token});},
    suspend(value){suspended=value;update(true);},
    receive(data){if(data.token===token&&ready()&&!suspended){for(let i=0;i<Math.min(data.count,128);i++){const id=data.values[2*i],v=data.values[2*i+1];if(Number.isInteger(id)&&id>0&&id<128)values[id]=Number.isFinite(v)?v:NaN;}for(const n of nodes)if(visible(n))paint(n);}send({type:'modAck',token:data.token});}
  };
}
