// SPDX-License-Identifier: GPL-3.0-only
// UI-only transactions. Store changed steps, never replace/restart the player.
export class RollHistory{
  constructor(model){this.model=model;this.epoch=null;this.banks=new Map();this.past=[];this.future=[];this.bytes=0;}
  sync(){if(this.epoch!==this.model.state.sequence){this.epoch=this.model.state.sequence;this.banks.clear();}let bank=this.banks.get(this.model.lane);if(!bank){bank={past:[],future:[]};this.banks.set(this.model.lane,bank);}this.past=bank.past;this.future=bank.future;}
  matches(entry,side){return entry&&this.model.state.sequence[entry.lane]&&entry.changes.every(c=>JSON.stringify(this.model.state.sequence[entry.lane][c.index])===c[side]);}
  canUndo(){this.sync();return !!this.matches(this.past.at(-1),'after');}
  canRedo(){this.sync();return !!this.matches(this.future.at(-1),'before');}
  capture(lane=this.model.lane){this.sync();return {epoch:this.epoch,lane,before:this.model.state.sequence[lane].map(s=>JSON.stringify(s))};}
  commit(snapshot){this.sync();if(snapshot.epoch!==this.epoch)return;const {lane,before}=snapshot,changes=[];this.model.state.sequence[lane].forEach((s,index)=>{const after=JSON.stringify(s);if(after!==before[index])changes.push({index,before:before[index],after});});if(!changes.length)return;
    const entry={lane,changes,bytes:changes.reduce((n,c)=>n+2*(c.before.length+c.after.length),0)};let bank=this.banks.get(lane);if(!bank){bank={past:[],future:[]};this.banks.set(lane,bank);}bank.past.push(entry);bank.future.length=0;let bytes=bank.past.reduce((n,e)=>n+e.bytes,0);while(bank.past.length&&(bank.past.length>64||bytes>2e6)){bytes-=bank.past.shift().bytes;}this.bytes=bytes;
  }
  run(action){const snapshot=this.capture();const result=action();this.commit(snapshot);return result;}
  apply(from,to,side){this.sync();const entry=from.at(-1);if(!this.matches(entry,side==='before'?'after':'before'))return false;from.pop();for(const c of entry.changes){const step=JSON.parse(c[side]);this.model.state.sequence[entry.lane][c.index]=step;this.model.publish(entry.lane,c.index,step);}to.push(entry);return true;}
  undo(){this.sync();return this.apply(this.past,this.future,'before');}
  redo(){this.sync();return this.apply(this.future,this.past,'after');}
}
