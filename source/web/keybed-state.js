// SPDX-License-Identifier: GPL-3.0-only
import {MpeEncoder} from './mpe.js';
export class KeybedState{
  constructor(emit){this.emit=emit;this.mpe=false;this.range=48;this.timbre=64/127;this.fingers=new Map();this.keys=new Map();this.encoder=new MpeEncoder(emit,{configureChannel:true});}
  legacyNotes(){return [...this.keys.values(),...(!this.mpe?[...this.fingers.values()].map(f=>f.note):[])].filter(n=>n!==null);}
  held(note){return this.legacyNotes().includes(note);}
  configure(mpe,range){
    range=Math.max(1,Math.min(96,Math.round(range||48)));const changed=this.mpe!==mpe;
    if(changed){this.stop();this.mpe=mpe;}
    if(range!==this.range){this.range=range;this.encoder.range=range;if(mpe)for(const[id,v]of this.encoder.voices){const s=0xb0+v.channel;this.emit([[s,101,0],[s,100,0],[s,6,range],[s,38,0],[s,101,127],[s,100,127]]);this.encoder.expression(id,{semitones:this.fingers.get(id)?.semitones||0});}}
    return changed;
  }
  begin(id,note,position){
    if(this.fingers.has(id))return;
    const f={...position,note,startX:position.x,startPitch:position.pitch,semitones:0};
    if(this.mpe){this.encoder.begin(id,note,100,position.pressure,this.timbre);for(const key of this.fingers.keys())if(!this.encoder.voices.has(key))this.fingers.delete(key);}
    else if(!this.held(note))this.emit([[0x90,note,100]]);
    this.fingers.set(id,f);
  }
  move(id,position){const f=this.fingers.get(id);if(!f)return;
    if(this.mpe){Object.assign(f,position);f.semitones=position.pitch-f.startPitch;this.encoder.expression(id,{semitones:f.semitones,pressure:position.pressure});}
    else if(position.note!==f.note){const previous=f.note;f.note=null;if(previous!==null&&!this.held(previous))this.emit([[0x80,previous,0]]);if(position.note!==null&&!this.held(position.note))this.emit([[0x90,position.note,100]]);f.note=position.note;}
  }
  end(id){const f=this.fingers.get(id);if(!f)return;this.fingers.delete(id);if(this.mpe)this.encoder.end(id);else if(f.note!==null&&!this.held(f.note))this.emit([[0x80,f.note,0]]);}
  press(id,note){if(this.keys.has(id))return;if(!this.held(note))this.emit([[0x90,note,100]]);this.keys.set(id,note);}
  release(id){const n=this.keys.get(id);if(n===undefined)return;this.keys.delete(id);if(!this.held(n))this.emit([[0x80,n,0]]);}
  setTimbre(value){this.timbre=Math.max(0,Math.min(1,value));for(const id of this.encoder.voices.keys())this.encoder.expression(id,{timbre:this.timbre});}
  stop(emit=true){if(emit){for(const id of [...this.encoder.voices.keys()])this.encoder.end(id);for(const n of new Set(this.legacyNotes()))this.emit([[0x80,n,0]]);}this.encoder.clear();this.fingers.clear();this.keys.clear();}
}
