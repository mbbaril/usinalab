// SPDX-License-Identifier: GPL-3.0-only
import {ScopeViewport,installScopeViewport} from './scope-viewport.js';
import {sourceColor} from './controls.js';
export class SourceHistory{
  constructor(size=150){this.values=new Float32Array(size);this.times=new Float64Array(size);this.reset();}
  reset(){this.pos=0;this.count=0;this.last=-Infinity;}
  push(value,time){if(!Number.isFinite(value)||!Number.isFinite(time)||time<=this.last)return false;this.values[this.pos]=value;this.times[this.pos]=time;this.pos=(this.pos+1)%this.values.length;this.count=Math.min(this.count+1,this.values.length);this.last=time;return true;}
  points(){return Array.from({length:this.count},(_,i)=>{const k=(this.pos-this.count+i+this.values.length)%this.values.length;return [this.times[k],this.values[k]];});}
}
// Display domains match fm::modSourceBipolar; FUNC's editor maps its selected polarity separately.
export const sourceBipolar=id=>id==='key'||/^(lfo|rand|comb|mac)/.test(id);
export function createSourceScopes({el,send,ready}){
  let current=null,token=0,suspended=false;
  const subscribe=()=>send({type:'scope',source:!suspended&&current?current.source.index:0,token:++token});
  function clear(){current?.cancel?.();current=null;subscribe();}
  function mount(root,source,{indicator,bipolar=sourceBipolar(source.id),caption=''}={}){
    current?.cancel?.();const view=new ScopeViewport();
    const history=new SourceHistory(),box=el('figure','source-scope'),label=el('figcaption'),value=el('output','','—');box.dataset.source=source.id;box.style.setProperty('--source-color',sourceColor(source.id));label.append(el('span','',source.name),value);box.append(label);
    const ns='http://www.w3.org/2000/svg',svg=document.createElementNS(ns,'svg'),path=document.createElementNS(ns,'path'),zero=document.createElementNS(ns,'line');svg.setAttribute('viewBox','0 0 600 110');svg.setAttribute('preserveAspectRatio','none');svg.setAttribute('aria-hidden','true');zero.setAttribute('x1',0);zero.setAttribute('x2',600);zero.setAttribute('y1',bipolar?55:106);zero.setAttribute('y2',bipolar?55:106);svg.append(zero,path);if(!indicator)box.append(svg);else box.classList.add('scope-indicator');if(caption){const description=el('small');description.dataset.readback='';description.sync=()=>description.textContent=typeof caption==='function'?caption():caption;description.sync();box.append(description);}root.append(box);
    const tools=el('div','scope-view-tools'),viewLabel=el('output'),reset=el('button','','1:1');reset.setAttribute('aria-label','Restaurar janela do scope');tools.append(viewLabel,reset);if(!indicator)box.append(tools);
    function draw(){const points=history.points();if(!points.length)return;const time=points.at(-1)[0];view.constrain(time-points[0][0]);box.dataset.seconds=view.seconds.toFixed(3);box.dataset.gain=view.gain.toFixed(3);box.dataset.age=view.age.toFixed(3);viewLabel.textContent=`${view.seconds<1?Math.round(view.seconds*1000)+' ms':view.seconds.toFixed(2)+' s'} · ×${view.gain.toFixed(2)}${view.age>.01?' · −'+view.age.toFixed(2)+' s':''}`;
      const start=time-view.age-view.seconds,end=time-view.age,y=x=>106-Math.max(0,Math.min(1,bipolar?(x*view.gain+1)/2:x*view.gain))*102;let d='',last=-Infinity;for(const[t,x]of points){if(t<start||t>end)continue;d+=`${t-last>.2?'M':'L'}${((t-start)/view.seconds*600).toFixed(1)} ${y(x).toFixed(1)}`;last=t;}path.setAttribute('d',d);
    }
    const cancel=indicator?()=>{}:installScopeViewport({svg,view,paint:draw});reset.onclick=()=>{cancel();view.reset();draw();};
    current={source,history,cancel,paint(v,time){if(!history.push(v,time))return;box.dataset.value=v;box.dataset.samples=history.count;value.textContent=v.toFixed(2);if(indicator){indicator(v);return;}draw();}};subscribe();return box;
  }
  return {mount,clear,suspend(value){if(suspended===value)return;suspended=value;if(value){current?.cancel?.();current?.history.reset();}subscribe();},receive(data){if(data.token===token&&current&&ready()&&!suspended)current.paint(data.value,data.time);send({type:'scopeAck',token:data.token});}};
}
