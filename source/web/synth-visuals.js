// SPDX-License-Identifier: GPL-3.0-only
import {GraphViewport,installEditableGraph} from './graph-viewport.js';
// Geometry follows AlgoView and EnvelopeView in the plugin editor.
const svg=(name,attrs={})=>{const n=document.createElementNS('http://www.w3.org/2000/svg',name);for(const [k,v]of Object.entries(attrs))n.setAttribute(k,v);return n;};
export function algorithmState(data,algorithm,feedback,param){
  if(algorithm===40)return {carriers:param('direct_out')&63,links:Array.from({length:36},(_,i)=>param(`mx_${1+Math.floor(i/6)}to${1+i%6}`)/100)};
  const [a,b]=data.algorithms[algorithm].states;
  return {carriers:a.carriers,links:a.links.map((v,i)=>v+(b.links[i]-v)*feedback/100)};
}
export function algorithmSvg({links,carriers}){
  const root=svg('svg',{viewBox:'0 0 150 110','aria-hidden':'true'}),depth=Array(6).fill(0),state=Array(6).fill(0),back=new Set();
  function walk(u){state[u]=1;let max=-1;for(let t=0;t<6;t++){if(t===u||links[u*6+t]<=0)continue;if(state[t]===1){back.add(`${u}:${t}`);continue;}max=Math.max(max,state[t]===2?depth[t]:walk(t));}state[u]=2;return depth[u]=max+1;}
  for(let u=0;u<6;u++)if(!state[u])walk(u);
  const rows=Math.max(...depth)+1,x=Array(6).fill(0);
  for(let r=0;r<rows;r++){let rank=0;const row=[];for(let u=0;u<6;u++){if(depth[u]!==r)continue;let want=rank++,n=0;if(r>0){want=0;for(let t=0;t<6;t++)if(t!==u&&links[u*6+t]>0&&!back.has(`${u}:${t}`)){want+=x[t];n++;}want=n?want/n:rank;}row.push([want,u]);}row.sort((a,b)=>a[0]-b[0]||a[1]-b[1]);let last=-1e9;for(const [w,u]of row){x[u]=Math.max(w,last+1);last=x[u];}}
  const xmin=Math.min(...x),cols=Math.max(...x)-xmin+1,cw=138/cols,ch=94/rows,bs=Math.min(cw,ch)*.62;
  const pos=u=>[6+(x[u]-xmin+.5)*cw,4+(rows-1-depth[u]+.5)*ch];
  for(let u=0;u<6;u++)for(let t=0;t<6;t++){if(links[u*6+t]<=0)continue;const [px,py]=pos(u),[qx,qy]=pos(t),loop=back.has(`${u}:${t}`);root.append(svg('path',{d:u===t?`M${px+bs/2} ${py-bs/2}a${bs*.3} ${bs*.3} 0 1 1 2 2`:loop?`M${px+bs/2} ${py}C${px+bs*1.3} ${py} ${qx+bs*1.3} ${qy} ${qx+bs/2} ${qy}`:`M${px} ${py+bs/2}L${qx} ${qy-bs/2}`,fill:'none',stroke:'currentColor','stroke-opacity':links[u*6+t]<1?.4+.5*links[u*6+t]:.85}));}
  const outputs=[];for(let u=0;u<6;u++)if(carriers>>u&1){const [px,py]=pos(u);outputs.push(px);root.append(svg('path',{d:`M${px} ${py+bs/2}V105`,stroke:'currentColor'}));}
  if(outputs.length)root.append(svg('path',{d:`M${Math.min(...outputs)-3} 105H${Math.max(...outputs)+3}`,stroke:'currentColor','stroke-width':2}));
  for(let u=0;u<6;u++){const [px,py]=pos(u),out=carriers>>u&1,used=out||links.some((v,i)=>v>0&&(Math.floor(i/6)===u||i%6===u));root.append(svg('rect',{x:px-bs/2,y:py-bs/2,width:bs,height:bs,rx:2,fill:out?'currentColor':'#101010',stroke:'currentColor',opacity:used?1:.25}));const text=svg('text',{x:px,y:py,'dominant-baseline':'central','text-anchor':'middle',fill:out?'#000':'#ddd','font-size':Math.max(8,bs*.55)});text.textContent=u+1;root.append(text);}
  return root;
}
export function envelopeGeometry(param,prefix,times){
  const env=prefix.startsWith('op')?`${prefix}env_`:prefix;
  const a=param(`${prefix}atk`),d=param(`${prefix}dec`),s=Math.max(0,Math.min(100,param(`${prefix}sus`)))/100,r=param(`${prefix}rel`),curve=param(`${prefix}curve`),mode=param(`${env}mode`),ac=param(`${env}atk_curve`),rc=param(`${env}rel_curve`);
  const width=v=>.04+Math.sqrt((times[Math.max(0,Math.min(127,v))]||0)/1000);
  const wa=width(a),wd=width(d),wr=width(r),ws=mode===1?0:.35,sx=296/(wa+wd+ws+wr),xs=[2,2+wa*sx,2+(wa+wd)*sx,2+(wa+wd+ws)*sx,298],points=[[2,70]];
  for(let i=1;i<=24;i++){const t=i/24,sh=t+ac/10*((1-(1-t)**3)-t);points.push([2+(xs[1]-2)*t,70-sh*68]);}
  function decay(from,to,start,end,cv){for(let i=1;i<=24;i++){const t=i/24,lin=from+(to-from)*t,ex=to+(from-to)*1000**-t;points.push([start+(end-start)*t,70-(lin+(ex-lin)*cv/10)*68]);}}
  decay(1,s,xs[1],xs[2],curve);points.push([xs[3],70-s*68]);decay(s,0,xs[3],xs[4],rc<0?curve:rc);
  return {path:points.map((p,i)=>`${i?'L':'M'}${p[0].toFixed(3)} ${p[1].toFixed(3)}`).join(''),xs,sx,widths:[wa,wd,wr],s,mode};
}
export function envelopeView({param,prefix,times,setParam,interactive=false}){
  const root=svg('svg',{viewBox:'0 0 300 72',class:`envelope-view${interactive?' editable-envelope':''}`,role:'img','aria-label':`Envelope ${prefix.replaceAll('_',' ').trim()}`}),fill=svg('path',{class:'envelope-fill'}),line=svg('path',{class:'envelope-line'}),handles=svg('g',{class:'envelope-handles'});root.append(fill,line,handles);
  let geometry,drag;
  root.sync=()=>{geometry=envelopeGeometry(param,prefix,times);line.setAttribute('d',geometry.path);fill.setAttribute('d',`${geometry.path}L298 70Z`);handles.replaceChildren(...[[geometry.xs[1],2],[geometry.xs[2],70-geometry.s*68],[geometry.xs[4],70]].map(([x,y])=>svg('circle',{cx:x,cy:y,r:3})));};
  if(interactive){
    const view=new GraphViewport(),wrapper=document.createElement('div'),tools=document.createElement('div'),readout=document.createElement('output'),reset=document.createElement('button');wrapper.className='envelope-viewport';wrapper.dataset.envelopePrefix=prefix;tools.className='envelope-view-tools';reset.textContent='1:1';reset.setAttribute('aria-label',`Restaurar vista do envelope ${prefix}`);tools.append(readout,reset);const help=document.createElement('div');help.className='envelope-stage-help';help.textContent='ATTACK ↔ · DECAY ↔ / SUSTAIN ↕ · RELEASE ↔';help.setAttribute('aria-label','Três alças: Attack ajusta tempo; a central ajusta Decay na horizontal e Sustain na vertical; Release ajusta tempo.');wrapper.append(root,help,tools);root.setAttribute('preserveAspectRatio','none');
    const paint=()=>{root.setAttribute('viewBox',`${view.x} ${view.y} ${view.width} ${view.height}`);root.dataset.zoomX=(300/view.width).toFixed(3);root.dataset.zoomY=(72/view.height).toFixed(3);readout.textContent=`Tempo ×${(300/view.width).toFixed(1)} · Nível ×${(72/view.height).toFixed(1)}`;};
    const cancel=installEditableGraph({svg:root,view,paint,beginEdit:p=>{const indices=[1,2,4],i=indices.reduce((best,k)=>Math.abs(geometry.xs[k]-p.x)<Math.abs(geometry.xs[best]-p.x)?k:best,1),stage=i===1?0:i===2?1:2;return {x:p.x,width:geometry.widths[stage],sx:geometry.sx,stage};},moveEdit:(drag,p)=>{const w=drag.width+(p.x-drag.x)/drag.sx,ms=Math.max(0,w-.04)**2*1000;let index=0;for(let i=1;i<128;i++)if(Math.abs(times[i]-ms)<Math.abs(times[index]-ms))index=i;setParam(`${prefix}${['atk','dec','rel'][drag.stage]}`,index);if(drag.stage===1)setParam(`${prefix}sus`,Math.round(Math.max(0,Math.min(100,100-p.y/72*100))));root.sync();}});
    reset.onclick=()=>{cancel();view.reset();paint();};wrapper.sync=root.sync;wrapper.cancelGraphGestures=cancel;root.sync();paint();return wrapper;
  }
  root.sync();return root;
}
