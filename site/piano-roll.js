// SPDX-License-Identifier: GPL-3.0-only
import {noteName} from './sequence-model.js';
import {inScale,quantizeNote} from './sequence-harmony.js';
import {captureNotes,transferNotes,resizeNotes,durationDelta,noteKey} from './roll-notes.js';
import {RollHistory} from './roll-history.js';
import {NoteDeleteGesture} from './note-delete-gesture.js';
const clamp=(n,a,b)=>Math.max(a,Math.min(b,n));
export class RollViewport {
  constructor(start=0,low=48){this.start=start;this.span=16;this.low=low;this.rows=13;}
  constrain(length){this.span=clamp(this.span,Math.min(4,length),length);this.start=clamp(this.start,0,length-this.span);this.rows=clamp(this.rows,5,36);this.low=clamp(this.low,0,128-this.rows);}
  point(x,y){return {step:this.start+x*this.span,pitch:this.low+this.rows-.5-y*this.rows};}
  navigate(initial,a,b,length){const mid={x:(a.x+b.x)/2,y:(a.y+b.y)/2},dx=Math.abs(a.x-b.x),dy=Math.abs(a.y-b.y);
    this.span=initial.view.span/(initial.dx>=.03?Math.max(.02,dx)/initial.dx:1);
    this.rows=initial.view.rows/(initial.dy>=.07?Math.max(.02,dy)/initial.dy:1);
    this.constrain(length);this.start=initial.anchor.step-mid.x*this.span;this.low=initial.anchor.pitch-this.rows+.5+mid.y*this.rows;this.constrain(length);
  }
}
export function createPianoRoll({el,host,tools,model,view,scale,root,changed,cursors,playing,selectPitch,shownNote,history=new RollHistory(model),clipboard={notes:[]},conflictPolicy=()=>'block',beforeCommand=()=>{}}){
  const labels=el('div','roll-labels'),roll=el('div','piano-roll'),blobs=el('div','roll-notes'),ruler=el('div','roll-ruler'),grid=el('div','roll-grid'),readout=el('output'),timeline=el('div','roll-time-ruler');timeline.setAttribute('aria-label','Destino de colagem: toque em um passo da régua');
  host.classList.add('roll-surface');host.append(labels,roll,timeline);roll.append(grid,ruler,blobs);tools.append(readout);
  roll.setAttribute('tabindex','0');roll.setAttribute('aria-label','Piano roll: toque adiciona; dois toques no corpo da nota apagam; arraste move; borda direita ajusta duração; dois dedos navegam e ampliam');
  const deletion=new NoteDeleteGesture(),undo=el('button','roll-undo','DESFAZER'),redo=el('button','roll-redo','REFAZER'),select=el('button','roll-select','SELECIONAR'),erase=el('button','roll-erase','APAGAR SELEÇÃO'),copy=el('button','roll-copy','COPIAR'),paste=el('button','roll-paste','COLAR'),duplicate=el('button','roll-duplicate','DUPLICAR');const actions=el('div','roll-edit-actions');actions.append(select,erase,copy,paste,duplicate);tools.append(undo,redo,actions);
  let selecting=false;const selected=new Set(),key=(i,n)=>`${i}:${n}`;
  const syncUndo=()=>{undo.disabled=!history.canUndo();redo.disabled=!history.canRedo();erase.disabled=!selected.size;copy.disabled=duplicate.disabled=!selected.size;paste.disabled=!clipboard.notes.length;erase.textContent=selected.size?`APAGAR (${selected.size})`:'APAGAR SELEÇÃO';select.setAttribute('aria-pressed',selecting);};
  function transaction(action){history.run(action);deletion.clear();changed();paint();}
  function remove(i,n){cancel();transaction(()=>{const s=model.steps[i];if(!s.on||!model.notes(s).includes(n))return;model.write(s,model.notes(s).filter(p=>p!==n));model.edit(i,{});});selected.delete(key(i,n));paint();}
  undo.onclick=()=>{cancel();if(history.undo()){selected.clear();changed();}paint();};redo.onclick=()=>{cancel();if(history.redo()){selected.clear();changed();}paint();};
  select.onclick=()=>{cancel();selecting=!selecting;paint();};erase.onclick=()=>{cancel();transaction(()=>{for(let i=0;i<model.steps.length;i++){const s=model.steps[i];if(!s.on)continue;const notes=model.notes(s),remaining=notes.filter(n=>!selected.has(key(i,n)));if(remaining.length!==notes.length){model.write(s,remaining);model.edit(i,{});}}});selected.clear();paint();};
  const feedback=el('output','roll-feedback');tools.append(feedback);
  function apply(notes,options){const result=transferNotes(model,notes,{canonical:!!scale(),policy:conflictPolicy(),...options});feedback.textContent=result.ok?'':result.error;if(result.ok){selected.clear();for(const k of result.selected)selected.add(k);const [step,pitch]=[...selected][0].split(':').map(Number);model.select(step);selectPitch(pitch);if(step<view.start)view.start=step;else if(step>=view.start+view.span)view.start=step-Math.max(0,view.span-4);}return result;}
  copy.onclick=()=>{cancel();clipboard.notes=captureNotes(model,selected,!!scale());feedback.textContent=`${clipboard.notes.length} notas copiadas. Destino: toque na régua.`;paint();};
  paste.onclick=()=>{cancel();if(!clipboard.notes.length)return;const first=Math.min(...clipboard.notes.map(n=>n.step));transaction(()=>apply(clipboard.notes,{deltaStep:model.selected-first}));};
  duplicate.onclick=()=>{cancel();const notes=captureNotes(model,selected,!!scale());if(!notes.length)return;const first=Math.min(...notes.map(n=>n.step)),end=Math.max(...notes.map(n=>n.step+n.duration));transaction(()=>apply(notes,{deltaStep:Math.max(1,Math.ceil(end-first))}));};
  timeline.onpointerdown=e=>{e.preventDefault();e.stopPropagation();cancel();model.select(point(local(e)).step);changed();paint();};
  const marquee=el('div','roll-selection-box');marquee.hidden=true;const cursor=el('div','roll-insert-cursor');roll.append(marquee,cursor);
  const pointers=new Map();let edit=null,navigation=null,navigating=false;
  const local=e=>{const r=roll.getBoundingClientRect();return {x:(e.clientX-r.left)/r.width,y:(e.clientY-r.top)/r.height};};
  const point=p=>{const v=view.point(p.x,p.y);return {step:clamp(Math.floor(v.step),0,model.settings.length-1),pitch:quantizeNote(clamp(Math.round(v.pitch),0,127),scale(),root())};};
  function geometry(b,i,n,gate){b.style.left=`${(i-view.start)/view.span*100}%`;b.style.top=`${(view.low+view.rows-1-n)/view.rows*100}%`;b.style.width=`${gate/view.span*100}%`;b.style.height=`${100/view.rows}%`;}
  function paint(){for(const k of selected){const[i,n]=k.split(':').map(Number),s=model.steps[i];if(!s?.on||!model.notes(s).includes(n))selected.delete(k);}syncUndo();cursor.style.left=`${(model.selected-view.start)/view.span*100}%`;view.constrain(model.settings.length);roll.dataset.start=view.start.toFixed(3);roll.dataset.span=view.span.toFixed(3);roll.dataset.low=view.low.toFixed(3);roll.dataset.rows=view.rows.toFixed(3);
    roll.style.setProperty('--roll-column',`${100/view.span}%`);roll.style.setProperty('--roll-row',`${100/view.rows}%`);
    roll.style.setProperty('--roll-x',`${-view.start/view.span*100}%`);roll.style.setProperty('--roll-y',`${(view.low%1)/view.rows*100}%`);
    readout.textContent=`${noteName(Math.ceil(view.low))} – ${noteName(Math.min(127,Math.floor(view.low+view.rows-1)))} · ${Math.ceil(view.span)} passos`;
    labels.replaceChildren();grid.replaceChildren();for(let n=Math.ceil(view.low);n<=Math.min(127,Math.floor(view.low+view.rows));n++){const l=el('span','',noteName(n));l.style.top=`${(view.low+view.rows-1-n)/view.rows*100}%`;l.style.height=`${100/view.rows}%`;l.classList.toggle('in-scale',inScale(n,scale(),root()));labels.append(l);const band=el('div','roll-pitch-band');band.style.top=l.style.top;band.style.height=l.style.height;band.classList.toggle('black',[1,3,6,8,10].includes(n%12));grid.append(band);}
    ruler.replaceChildren();timeline.replaceChildren();for(let i=Math.ceil(view.start);i<Math.min(model.settings.length,view.start+view.span);i++){const l=el('span','',String(i+1));l.style.left=`${(i-view.start)/view.span*100}%`;const tick=el('span','',String(i+1));tick.style.left=l.style.left;timeline.append(tick);l.textContent='';ruler.append(l);}
    if(edit)return;
    blobs.replaceChildren();for(let i=0;i<Math.min(model.settings.length,Math.ceil(view.start+view.span));i++){
      const s=model.steps[i];if(!s.on)continue;for(const n of model.notes(s)){const length=model.duration(s,n);if(i+length<view.start)continue;if(n<view.low-1||n>view.low+view.rows)continue;
        const b=el('button','roll-note'),title=el('span','roll-note-name',noteName(n)),edge=el('span','roll-gate-edge');b.append(title,edge);b.dataset.step=i;b.dataset.note=n;b.setAttribute('aria-label',`${noteName(n)}, passo ${i+1}, duração ${length.toFixed(2)} passos`);b.dataset.duration=length;geometry(b,i,n,length);
        b.classList.toggle('selected',selected.has(key(i,n)));b.classList.toggle('mono-muted',!!model.settings.mono&&n!==shownNote(s));b.classList.toggle('cursor',cursors()[model.lane]===i&&playing());
        b.onkeydown=e=>{if(e.key==='Enter'){model.select(i);selectPitch(n);selected.clear();selected.add(key(i,n));changed();paint();}};blobs.append(b);
      }
    }
  }
  roll.onpointerdown=e=>{if(!pointers.size)beforeCommand();roll.focus?.({preventScroll:true});if(e.pointerType==='mouse'&&e.button!==0)return;e.preventDefault();pointers.set(e.pointerId,local(e));roll.dataset.contacts=pointers.size;roll.setPointerCapture(e.pointerId);
    if(pointers.size>=2){marquee.hidden=true;deletion.clear();edit=null;navigating=true;const[a,b]=[...pointers.values()],mid={x:(a.x+b.x)/2,y:(a.y+b.y)/2};navigation={view:{...view},anchor:view.point(mid.x,mid.y),dx:Math.abs(a.x-b.x),dy:Math.abs(a.y-b.y)};paint();return;}
    if(navigating)return;const hit=e.target.closest('.roll-note'),p=point(local(e));
    if(hit){const i=+hit.dataset.step,n=+hit.dataset.note;if(selecting){edit={id:e.pointerId,selection:true,from:i,pitch:n,x:e.clientX,y:e.clientY,moved:false};return;}if(!selected.has(key(i,n))){selected.clear();selected.add(key(i,n));}model.select(i);selectPitch(n);const r=hit.getBoundingClientRect();edit={id:e.pointerId,hit,from:i,pitch:n,to:i,next:n,gate:model.duration(model.steps[i],n),resize:e.clientX>=r.right-Math.min(12,r.width*.3),x:e.clientX,y:e.clientY,moved:false,members:captureNotes(model,selected,!!scale())};changed();}
    else edit={id:e.pointerId,add:!selecting,selection:selecting,...p,x:e.clientX,y:e.clientY,moved:false};
  };
  roll.onpointermove=e=>{if(!pointers.has(e.pointerId))return;e.preventDefault();pointers.set(e.pointerId,local(e));
    if(navigating){if(pointers.size===2&&navigation){const[a,b]=[...pointers.values()];view.navigate(navigation,a,b,model.settings.length);paint();}return;}
    if(!edit||edit.id!==e.pointerId)return;const d=edit;if(Math.hypot(e.clientX-d.x,e.clientY-d.y)<6&&!d.moved)return;d.moved=true;deletion.clear();
    if(d.selection){if(d.from===undefined){const a=local({clientX:d.x,clientY:d.y}),b=local(e);marquee.hidden=false;marquee.style.left=`${Math.min(a.x,b.x)*100}%`;marquee.style.top=`${Math.min(a.y,b.y)*100}%`;marquee.style.width=`${Math.abs(a.x-b.x)*100}%`;marquee.style.height=`${Math.abs(a.y-b.y)*100}%`;}return;}if(d.add)return;const p=point(local(e));
    if(d.resize){const r=roll.getBoundingClientRect();d.deltaGate=durationDelta(d.members,Math.round((e.clientX-d.x)/r.width*view.span*100)/100);for(const b of blobs.children){const m=d.members.find(n=>n.step===+b.dataset.step&&n.pitch===+b.dataset.note);if(m)geometry(b,m.step,m.pitch,m.duration+d.deltaGate);}}
    else {const r=roll.getBoundingClientRect();d.to=clamp(d.from+Math.round((e.clientX-d.x)/r.width*view.span),0,model.settings.length-1);d.next=quantizeNote(clamp(d.pitch-Math.round((e.clientY-d.y)/r.height*view.rows),0,127),scale(),root());geometry(d.hit,d.to,d.next,d.gate);d.hit.firstChild.textContent=noteName(d.next);if(d.members.length>1){const ds=d.to-d.from,dp=d.next-d.pitch;for(const b of blobs.children){const m=d.members.find(n=>n.step===+b.dataset.step&&n.pitch===+b.dataset.note);if(m){geometry(b,m.step+ds,model.quantize(m.pitch+dp),m.duration);b.firstChild.textContent=noteName(model.quantize(m.pitch+dp));}}}}
  };
  const end=e=>{if(!pointers.has(e.pointerId)||e.type==='lostpointercapture'&&roll.hasPointerCapture(e.pointerId))return;if(e.type==='pointerup')roll.onpointermove(e);const d=edit;marquee.hidden=true;pointers.delete(e.pointerId);roll.dataset.contacts=pointers.size;if(roll.hasPointerCapture(e.pointerId))roll.releasePointerCapture(e.pointerId);edit=null;
    if(navigating){if(!pointers.size){navigating=false;navigation=null;}paint();return;}
    if(e.type!=='pointerup')deletion.clear();
    if(e.type==='pointerup'&&d){if(d.selection){deletion.clear();if(d.from!==undefined&&!d.moved){const k=key(d.from,d.pitch);if(selected.has(k))selected.delete(k);else selected.add(k);}else if(d.moved){const a=local({clientX:d.x,clientY:d.y}),b=local(e),left=view.point(Math.min(a.x,b.x),0).step,right=view.point(Math.max(a.x,b.x),0).step,low=view.point(0,Math.max(a.y,b.y)).pitch-.5,high=view.point(0,Math.min(a.y,b.y)).pitch+.5;for(let i=0;i<model.settings.length;i++)if(model.steps[i].on)for(const n of model.notes(model.steps[i]))if(n>=low&&n<=high&&i<=right&&i+model.duration(model.steps[i],n)>=left)selected.add(key(i,n));}changed();paint();return;}history.run(()=>{if(d.add&&!d.moved){deletion.clear();const s=model.steps[d.step],existing=s.on?model.notes(s):[];if(existing.length<16||existing.includes(d.pitch)){model.write(s,[...existing,d.pitch]);model.edit(d.step,{});model.select(d.step);selectPitch(d.pitch);}}
      else if(!d.add){if(d.resize&&d.moved){const result=resizeNotes(model,d.members,d.deltaGate??0,{canonical:!!scale()});feedback.textContent=result.ok?'':result.error;}else if(d.moved&&d.members.length>1){apply(d.members,{move:true,deltaStep:d.to-d.from,deltaPitch:d.next-d.pitch});}else if(d.moved){apply(d.members,{move:true,deltaStep:d.to-d.from,deltaPitch:d.next-d.pitch});}}
      });
      if(!d.add&&!d.moved&&!d.resize){if(deletion.tap(`${model.lane}:${d.from}:${d.pitch}`,e.clientX,e.clientY,e.timeStamp??performance.now()))remove(d.from,d.pitch);}else deletion.clear();
      changed();}
    paint();
  };
  roll.onpointerup=roll.onpointercancel=roll.onlostpointercapture=end;
  const cancel=()=>{beforeCommand();marquee.hidden=true;deletion.clear();edit=null;navigation=null;navigating=false;const ids=[...pointers.keys()];pointers.clear();roll.dataset.contacts=0;for(const id of ids)if(roll.hasPointerCapture(id))roll.releasePointerCapture(id);};
  const shortcuts=e=>{
    if(e.defaultPrevented||e.isComposing||e.altKey||['INPUT','TEXTAREA','SELECT'].includes(e.target?.tagName))return;
    const k=e.key.toLowerCase(),command=e.ctrlKey||e.metaKey;let action=null;
    if(command){if(k==='a')action=()=>{cancel();selected.clear();for(let i=0;i<model.settings.length;i++)if(model.steps[i].on)for(const n of model.notes(model.steps[i]))selected.add(key(i,n));paint();};
      if(k==='c')action=copy.onclick;if(k==='v')action=paste.onclick;if(k==='d')action=duplicate.onclick;if(k==='z')action=e.shiftKey?redo.onclick:undo.onclick;if(k==='y')action=redo.onclick;
    }else if(k==='escape')action=()=>{cancel();selected.clear();selecting=false;feedback.textContent='';paint();};
    else if(k==='delete'||k==='backspace'){action=()=>{if(!selected.size){const hit=e.target?.closest?.('.roll-note');if(hit)selected.add(key(+hit.dataset.step,+hit.dataset.note));}if(selected.size)erase.onclick();};}
    else if(['arrowleft','arrowright','arrowup','arrowdown'].includes(k)&&selected.size)action=()=>{cancel();const notes=captureNotes(model,selected,!!scale());transaction(()=>apply(notes,{move:true,deltaStep:k==='arrowright'?1:k==='arrowleft'?-1:0,deltaPitch:k==='arrowup'?1:k==='arrowdown'?-1:0}));};
    if(action){e.preventDefault();e.stopPropagation?.();action();}
  };host.onkeydown=tools.onkeydown=shortcuts;
  return {paint,cancel,deleteNote:remove,syncUndo,shiftOctave(delta){cancel();view.low+=delta*12;view.constrain(model.settings.length);paint();}};
}
