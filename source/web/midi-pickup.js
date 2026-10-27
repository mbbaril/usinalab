// SPDX-License-Identifier: GPL-3.0-only
export class MidiPickup {
  constructor(){this.runtime=new Map();}
  reset(id){if(id===undefined)this.runtime.clear();else for(const [map] of this.runtime)if(map.param===id)this.runtime.delete(map);}
  next(map,value,current){
    let r=this.runtime.get(map);if(!r){r={latched:false,last:null,output:current};this.runtime.set(map,r);}
    if(r.latched&&Math.abs(current-r.output)>1e-6){r.latched=false;r.last=null;}
    const reached=Math.abs(value-current)<=1/127||r.last!==null&&(r.last-current)*(value-current)<=0;
    r.last=value;if(!r.latched&&!reached)return null;r.latched=true;return value;
  }
  applied(map,value){const r=this.runtime.get(map);if(r)r.output=value;}
  waiting(map){return !this.runtime.get(map)?.latched;}
}
