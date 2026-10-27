// SPDX-License-Identifier: GPL-3.0-only
import {mappedDurations,noteDuration} from './note-duration.js';
export const noteName=n=>['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'][n%12]+(Math.floor(n/12)-1);
export const notesOf=s=>s.notes?.length?s.notes:[s.note];
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
export function writeNotes(step,notes,durations=step.durations){const values=[...new Set(notes.map(n=>clamp(Math.round(n),0,127)))].sort((a,b)=>a-b).slice(0,16);step.durations=durations?Object.fromEntries(Object.entries(durations).filter(([n])=>values.includes(+n))):undefined;if(step.durations&&!Object.keys(step.durations).length)step.durations=undefined;step.notes=values;step.on=values.length>0;if(values.length)step.note=values[0];step.inherit=false;}
export class SequenceEditor{
  constructor(state,publish,changed=()=>{}){this.state=state;this.publish=publish;this.changed=changed;this.lane=0;this.selected=0;this.page=0;this.mode='steps';this.armed=false;this.held=new Set();this.recording=null;this.routes=new Map();this.counts=new Map();this.quantize=n=>n;this.notes=notesOf;this.write=writeNotes;this.durationMap=s=>s.durations;this.duration=(s,n)=>noteDuration(s,n,this.settings);this.mpe=false;this.members=0;this.rpnMsb=new Int16Array(16).fill(127);this.rpnLsb=new Int16Array(16).fill(127);}
  get settings(){return this.state.lanes[this.lane];}
  get steps(){return this.state.sequence[this.lane];}
  get melodic(){return this.lane===0||this.lane>=5;}
  get step(){return this.steps[this.selected];}
  edit(i,values){Object.assign(this.steps[i],values);this.publish(this.lane,i,this.steps[i]);}
  select(i){this.selected=clamp(i,0,this.settings.length-1);this.page=Math.floor(this.selected/16);}
  setLane(lane){this.cancel();this.lane=lane;if(!this.melodic)this.mode='steps';this.select(this.selected);}
  cancel(){this.held.clear();this.recording=null;this.armed=false;}
  resetInput(){this.cancel();this.routes.clear();this.counts.clear();this.rpnMsb.fill(127);this.rpnLsb.fill(127);}
  configureMpe(enabled,force=false){if(force||enabled!==this.mpe){this.mpe=enabled;this.members=enabled?15:0;this.resetInput();}}
  clearChannel(channel,advance=false){for(const[key,pitch]of this.routes){if(!key.includes(`/${channel}:`))continue;if(pitch!==null){const output=`${channel}:${pitch}`,count=(this.counts.get(output)||1)-1;if(count>0)this.counts.set(output,count);else this.counts.delete(output);}this.routes.set(key,null);if(advance)this.endRecording(key);else this.held.delete(key);}}
  endRecording(key){if(!this.held.delete(key))return;if(!this.held.size&&this.recording!==null){this.recording=null;this.select((this.selected+1)%this.settings.length);this.changed();}}
  midi(status,note,velocity,active=true,origin='keybed'){
    const kind=status&0xf0,channel=status&15,key=`${origin}/${channel}:${note}`,raw=[status,note,velocity],member=this.mpe&&channel>0&&channel<=this.members;
    if(kind===0xb0){
      if(note===101)this.rpnMsb[channel]=velocity;if(note===100)this.rpnLsb[channel]=velocity;
      if(note===6&&channel===0&&this.mpe&&this.rpnMsb[0]===0&&this.rpnLsb[0]===6){this.resetInput();this.members=clamp(velocity,0,15);this.changed();}
      if(note===120||note===123){if(member){this.cancel();this.clearChannel(channel);}else this.resetInput();this.changed();}
      return raw;
    }
    if(kind===0x80||kind===0x90&&velocity===0){
      if(!this.routes.has(key))return raw;const pitch=this.routes.get(key);this.routes.delete(key);this.endRecording(key);if(pitch===null)return false;
      const output=`${status&15}:${pitch}`,count=(this.counts.get(output)||1)-1;if(count>0){this.counts.set(output,count);return false;}this.counts.delete(output);return [status,pitch,velocity];
    }
    if(kind===0xa0&&this.routes.has(key)){const pitch=this.routes.get(key);return pitch===null?false:[status,pitch,velocity];}
    if(kind!==0x90||velocity<=0)return raw;
    if(this.routes.has(key)){const pitch=this.routes.get(key);if(pitch!==null)return [status,pitch,velocity];if(this.held.has(key))return false;this.routes.delete(key);}
    if(member)this.clearChannel(channel,true);
    const recording=this.armed&&this.melodic&&active;
    if(recording&&this.settings.mono&&this.held.size){this.held.add(key);this.routes.set(key,null);return false;}
    const pitch=recording?this.quantize(note):note;this.routes.set(key,pitch);const output=`${status&15}:${pitch}`,count=this.counts.get(output)||0;this.counts.set(output,count+1);
    if(recording){if(!this.held.size){this.recording=this.selected;this.step.pitchOffset=0;writeNotes(this.step,[]);}this.held.add(key);const s=this.steps[this.recording];writeNotes(s,[...(s.on?notesOf(s):[]),pitch]);s.velocity=clamp(velocity,1,127);this.publish(this.lane,this.recording,s);this.changed();}
    return count?false:[status,pitch,velocity];
  }
  move(from,pitch,to,next){next=this.quantize(next);if(from===to&&pitch===next)return;const origin=this.steps[from],target=this.steps[to];if(from!==to&&target.on&&this.notes(target).length>=16&&!this.notes(target).includes(next))return;
    const originalNotes=this.notes(origin),remaining=originalNotes.filter(n=>n!==pitch),durations=this.durationMap(origin),duration=durations?.[pitch];
    if(from===to){const nextNotes=originalNotes.map(n=>n===pitch?next:n),map=mappedDurations({durations},originalNotes,nextNotes);this.write(origin,nextNotes,map);this.publish(this.lane,from,origin);}
    else{const existing=target.on?this.notes(target):[],map={...this.durationMap(target)};if(duration!==undefined||map[next]!==undefined)map[next]=Math.max(duration??this.duration(origin,pitch),existing.includes(next)?this.duration(target,next):0);this.write(origin,remaining,durations);this.write(target,[...existing,next],map);this.publish(this.lane,from,origin);this.publish(this.lane,to,target);}this.select(to);this.changed();
  }
}
