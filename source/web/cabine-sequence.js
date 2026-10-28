// SPDX-License-Identifier: GPL-3.0-only
// Screen behavior from Cabine.cpp drumPress/cycleMelodyStep/muteTap and CabinePanel.cpp.
import {cabineScales,cabineNoteNames,cabineNoteName,installCabineKeys} from './cabine-keys.js';
export const drumLevel=s=>!s.on?0:s.velocity<=60?1:s.velocity<=100?2:3;
export function installDrumTouch({pads,resolve,publish,refresh,pulse}){
  const held=new Map(),owners=new Set();
  pads.forEach((pad,index)=>{
    pad.style.touchAction='none';
    const toggle=()=>{const {step,lane,stepIndex}=resolve(index);step.on=!step.on;publish(lane,stepIndex);pulse();refresh();};
    pad.onclick=e=>{if(e.detail===0)toggle();};
    pad.onpointerdown=e=>{if(e.pointerType==='mouse'&&e.button!==0||owners.has(index))return;e.preventDefault();const target=resolve(index);held.set(e.pointerId,{...target,index,y:e.clientY,accent:target.step.accent??0,hapticAccent:target.step.accent??0,drag:false});owners.add(index);pad.setPointerCapture(e.pointerId);};
    pad.onpointermove=e=>{const p=held.get(e.pointerId);if(!p)return;e.preventDefault();const delta=p.y-e.clientY;if(!p.drag&&Math.abs(delta)<6)return;const first=!p.drag;p.drag=true;p.step.accent=Math.max(0,Math.min(1,p.accent+delta/120));if(first||Math.abs(p.step.accent-p.hapticAccent)>=.24||[0,1].includes(p.step.accent)&&p.step.accent!==p.hapticAccent){pulse();p.hapticAccent=p.step.accent;}publish(p.lane,p.stepIndex);refresh();};
    const end=e=>{const p=held.get(e.pointerId);if(!p)return;if(e.type==='pointerup')pad.onpointermove(e);held.delete(e.pointerId);owners.delete(p.index);if(e.type==='pointerup'&&!p.drag){p.step.on=!p.step.on;publish(p.lane,p.stepIndex);pulse();refresh();}};
    pad.onpointerup=pad.onpointercancel=pad.onlostpointercapture=end;
  });
  return ()=>{held.clear();owners.clear();};
}
export function createCabineSequence({state,send,param,setParam}){
  let selected=-1;const lastMute=Array(4).fill(-1e9);const config=()=>state.cabine;
  const publish=(lane,index)=>{if(Number.isInteger(index)){const s=state.sequence[lane][index];send({type:'step',values:[lane,index,+s.on,s.note,s.velocity,s.probability,s.ratchet,s.gate],notes:s.notes,expression:s});}else send({type:'part',lane,settings:state.lanes[lane],steps:state.sequence[lane]});};
  function cycle(index){const c=config(),row=Math.floor(index/8),lane=c.melody?0:5+Math.floor(row/2),step=c.stepPage*16+(c.melody?index:row%2*8+index%8);if(c.melody&&row>=2)return;const s=state.sequence[lane][step],next=(drumLevel(s)+1)%4;s.on=next>0;if(next)s.velocity=[40,80,127][next-1];if(c.melody)selected=step;else c.drum=Math.floor(row/2);publish(lane);}
  function melodyNote(note){if(selected<0)return;const s=state.sequence[0][selected];s.note=note;s.notes=[note];s.inherit=false;if(!s.on){s.on=true;s.velocity=80;}publish(0);}
  function tune(delta){const lane=5+config().drum,settings=state.lanes[lane];settings.baseNote=Math.max(0,Math.min(127,(settings.baseNote??[36,41,48,55][config().drum])+delta));for(const s of state.sequence[lane])if(s.inherit)s.note=settings.baseNote;publish(lane);}
  function applyMutes(){const c=config();for(let t=0;t<4;++t){const lane=5+t,s=state.lanes[lane];s.mute=c.mutes[t]||c.solo>=0&&c.solo!==t;send({type:'lane',values:[lane,s.length,s.direction,s.mul,s.div,+s.mute]});}}
  function muteTap(track,time=performance.now()){const c=config();c.mutes[track]=!c.mutes[track];if(time-lastMute[track]<350){c.solo=c.solo===track?-1:track;lastMute[track]=-1e9;}else lastMute[track]=time;applyMutes();}
  function clear(){const lane=config().melody?0:5+config().drum;state.sequence[lane].forEach(s=>s.on=false);publish(lane);}
  function togglePlay(){const start=!param('seq_play')||!param('seq_on');if(start)setParam('seq_on',1);setParam('seq_play',start?1:0);}
  return {cycle,melodyNote,tune,muteTap,clear,togglePlay,publish,get selected(){return selected;},resetSelection(){selected=-1;}};
}
export function renderCabineSequence({root,toolbar,el,state,model,param,note,pulse,rerender,cursors}){
  const c=state.cabine,melody=c.melody,controls=el('div','cabine-sequence-controls'),button=(name,fn)=>{const b=el('button','',name);b.onclick=()=>{fn();refresh();};controls.append(b);return b;};
  root.classList.add('cabine-sequencing');root.append(toolbar,el('p','perform-note',melody?'MELODY: toque um passo para escolher sua força; toque uma tecla para atribuir a nota ao passo escolhido.':'DRUMS: toque liga/desliga; arraste vertical ajusta ACCENT. Duplo toque em MUTE alterna SOLO.'));
  const prev=button('STEPS ◀',()=>{c.stepPage=Math.max(0,c.stepPage-1);refresh();}),label=el('span','');controls.append(label);const next=button('▶ STEPS',()=>{c.stepPage=Math.min(3,c.stepPage+1);refresh();}),play=button('PLAY',()=>model.togglePlay()),clear=button('CLEAR',()=>model.clear());
  const mutes=[];
  if(!melody){for(let t=0;t<4;++t)mutes.push(button(`M${t+1}`,()=>model.muteTap(t)));button('NOTE −',()=>model.tune(-1));button('NOTE +',()=>model.tune(1));}
  else {
    for(const [key,choices] of [['root',cabineNoteNames],['scale',cabineScales.map(s=>s[0])]]){const select=el('select');select.setAttribute('aria-label',key==='root'?'Tônica da melodia':'Escala da melodia');choices.forEach((name,i)=>{const o=el('option','',name);o.value=i;select.append(o);});select.value=c[key];select.onchange=()=>{c[key]=+select.value;rerender();};controls.append(select);}
    button('OCT −',()=>{c.octave=Math.max(0,c.octave-1);rerender();});button('OCT +',()=>{c.octave=Math.min(9,c.octave+1);rerender();});
  }
  root.append(controls);const grid=el('div','cabine-grid');grid.setAttribute('aria-label',melody?'CABINE MELODY':'CABINE DRUMS');const pads=Array.from({length:64},(_,i)=>{const b=el('button','cabine-pad');b.dataset.pad=i;b.onclick=()=>{pulse();model.cycle(i);refresh();};grid.append(b);return b;});root.append(grid);
  const cancel=melody?installCabineKeys({pads,config:c,note,pulse,startRow:2,onNote:pitch=>{model.melodyNote(pitch);refresh();}}):installDrumTouch({pads,resolve:i=>{const row=Math.floor(i/8),lane=5+Math.floor(row/2),index=c.stepPage*16+row%2*8+i%8;c.drum=lane-5;return {lane,stepIndex:index,step:state.sequence[lane][index]};},publish:model.publish,refresh,pulse});
  function refresh(){
    label.textContent=`${c.stepPage*16+1}–${c.stepPage*16+16}${!melody?` · D${c.drum+1} ${cabineNoteName(state.lanes[5+c.drum].baseNote??[36,41,48,55][c.drum])}`:''}`;prev.disabled=c.stepPage===0;next.disabled=c.stepPage===3;play.textContent=param('seq_play')?'STOP':'PLAY';play.classList.toggle('active',!!param('seq_play'));clear.textContent=melody?'CLEAR':`CLEAR ${c.drum+1}`;
    mutes.forEach((b,t)=>{b.textContent=`${c.solo===t?'S':'M'}${t+1}`;b.setAttribute('aria-pressed',c.mutes[t]||c.solo===t);});
    for(let i=0;i<(melody?16:64);++i){const row=Math.floor(i/8),track=Math.floor(row/2),lane=melody?0:5+track,index=c.stepPage*16+(melody?i:row%2*8+i%8),step=state.sequence[lane][index],level=drumLevel(step),pad=pads[i];pad.textContent=`${index+1} · ${['OFF','WEAK','MID','STRONG'][level]}`;pad.setAttribute('aria-label',`${melody?'MELODY':`DRUM ${track+1}`} STEP ${index+1}: ${['OFF','WEAK','MID','STRONG'][level]}`);pad.setAttribute('aria-pressed',step.on);if(melody){pad.textContent=`${index+1} ${cabineNoteName(step.note)} · ${['OFF','WEAK','MID','STRONG'][level]}`;pad.setAttribute('aria-label',`MELODY STEP ${index+1}: ${cabineNoteName(step.note)}, ${['OFF','WEAK','MID','STRONG'][level]}`);}if(!melody){const accent=Math.round((step.accent??0)*100);pad.textContent=`${index+1} · ${step.on?'ON':'OFF'} · A ${accent}%`;pad.setAttribute('aria-label',`DRUM ${track+1} STEP ${index+1}: ${step.on?'ON':'OFF'}, ACCENT ${accent}%`);pad.dataset.accent=step.accent??0;pad.style.setProperty('--accent-height',`${(step.accent??0)*100}%`);}pad.dataset.level=level;pad.dataset.note=step.note;pad.dataset.step=index;pad.dataset.lane=lane;pad.style.setProperty('--pad-color',melody?'#c2f000':['#ff7a45','#ffb000','#25e0c4','#9d4edd'][track]);pad.style.setProperty('--strength',melody?[.1,.3,.6,1][level]:step.on?Math.max(.15,(step.velocity+(step.accent??0)*(127-step.velocity))/127):.1);pad.classList.toggle('active',step.on);pad.classList.toggle('outside-length',index>=state.lanes[lane].length);pad.classList.toggle('muted-lane',state.lanes[lane].mute);pad.classList.toggle('chosen-step',melody&&model.selected===index);pad.classList.toggle('cursor',cursors()[lane]===index&&!!param('seq_play'));}
  }
  root.refreshCabine=refresh;root.cancelGestures=cancel;refresh();
}
