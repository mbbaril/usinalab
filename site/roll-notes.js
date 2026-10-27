// SPDX-License-Identifier: GPL-3.0-only
import {setNoteDuration} from './note-duration.js';
const defaults={velocity:100,probability:1,ratchet:1,gate:.5,accent:0,late:0,condition:0,pitchOffset:0};
const clone=x=>JSON.parse(JSON.stringify(x));
export const noteKey=(step,pitch)=>`${step}:${pitch}`;
function expression(step,canonical){return Object.fromEntries(Object.entries(defaults).map(([k,v])=>[k,k==='pitchOffset'&&canonical?0:step[k]??v]));}
export function captureNotes(model,selected,canonical=false){const notes=[];for(let step=0;step<model.settings.length;step++){const s=model.steps[step];if(!s.on)continue;for(const pitch of model.notes(s))if(selected.has(noteKey(step,pitch)))notes.push({step,pitch,duration:model.duration(s,pitch),expression:expression(s,canonical)});}return notes;}
// Validate everything on detached steps before publishing anything. No partial group moves.
export function transferNotes(model,notes,{deltaStep=0,deltaPitch=0,move=false,canonical=false,policy='block'}={}){
  if(!notes.length)return {ok:false,error:'Selecione notas primeiro.'};
  if(move&&deltaStep===0&&deltaPitch===0)return {ok:true,selected:new Set(notes.map(n=>noteKey(n.step,n.pitch)))};
  const working=new Map(),get=i=>{if(!working.has(i))working.set(i,clone(model.steps[i]));return working.get(i);},targets=new Set(),result=[];
  for(const n of notes){const step=n.step+deltaStep,pitch=model.quantize(n.pitch+deltaPitch);if(!Number.isInteger(step)||step<0||step>=model.settings.length||pitch<0||pitch>127)return {ok:false,error:'O grupo ultrapassa os limites da pista ou das notas.'};const k=noteKey(step,pitch);if(targets.has(k))return {ok:false,error:'A escala reúne duas notas do grupo na mesma posição.'};targets.add(k);result.push({...n,step,pitch});}
  if(move)for(const n of notes){const s=get(n.step);if(!s.on||!model.notes(s).includes(n.pitch)||model.duration(s,n.pitch)!==n.duration||JSON.stringify(expression(s,canonical))!==JSON.stringify(n.expression))return {ok:false,error:'A origem mudou durante o gesto.'};}
  if(move)for(const n of notes){const s=get(n.step);model.write(s,model.notes(s).filter(p=>p!==n.pitch));}
  for(const n of result){const s=get(n.step),existing=s.on?model.notes(s):[];
    if(existing.includes(n.pitch))return {ok:false,error:'Já existe uma nota nessa posição.'};
    if(existing.length>=16)return {ok:false,error:'O passo já contém 16 notas.'};
    const same=JSON.stringify(expression(s,canonical))===JSON.stringify(n.expression);
    if(existing.length&&!same&&policy==='block')return {ok:false,error:'Velocity ou expressão diferentes no passo de destino.'};
    if(!existing.length||policy==='source')Object.assign(s,n.expression);
    const durations=s.on?{...model.durationMap(s)}:{};durations[n.pitch]=n.duration;model.write(s,[...existing,n.pitch],durations);
  }
  for(const[i,s]of working)if(JSON.stringify(s)!==JSON.stringify(model.steps[i])){model.steps[i]=s;model.edit(i,{});}
  return {ok:true,selected:new Set(result.map(n=>noteKey(n.step,n.pitch)))};
}

// Shared delta preserves rhythmic differences within the selection. Validate before publishing.
export function durationDelta(notes,delta){return Math.max(.01-Math.min(...notes.map(n=>n.duration)),Math.min(64-Math.max(...notes.map(n=>n.duration)),delta));}
export function resizeNotes(model,notes,delta,{canonical=false}={}){
  if(!notes.length||!Number.isFinite(delta))return {ok:false,error:'Selecione notas primeiro.'};
  if(delta===0)return {ok:true};
  const working=new Map();
  for(const n of notes){const source=model.steps[n.step];if(!source?.on||!model.notes(source).includes(n.pitch)||model.duration(source,n.pitch)!==n.duration||JSON.stringify(expression(source,canonical))!==JSON.stringify(n.expression))return {ok:false,error:'A origem mudou durante o gesto.'};
    const duration=Math.round((n.duration+delta)*100)/100;if(duration<.01||duration>64)return {ok:false,error:'A duração do grupo ultrapassa os limites.'};
    if(!working.has(n.step))working.set(n.step,clone(source));const step=working.get(n.step);model.write(step,model.notes(step));setNoteDuration(step,n.pitch,duration);
  }
  for(const[i,step]of working){model.steps[i]=step;model.edit(i,{});}
  return {ok:true};
}
