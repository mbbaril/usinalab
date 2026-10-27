// SPDX-License-Identifier: GPL-3.0-only
// Isomorphic layout and defaults from src/plugin/Cabine.cpp: kScales and notePitch.
export const cabineScales=[['MAJOR',[0,2,4,5,7,9,11]],['MINOR',[0,2,3,5,7,8,10]],['DORIAN',[0,2,3,5,7,9,10]],['PHRYGIAN',[0,1,3,5,7,8,10]],['MIXOLYDIAN',[0,2,4,5,7,9,10]],['HARMONIC MINOR',[0,2,3,5,7,8,11]],['PENTATONIC MINOR',[0,3,5,7,10]],['CHROMATIC',[0,1,2,3,4,5,6,7,8,9,10,11]]];
export const cabineNoteNames=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
export function cabinePitch(row,col,{scale=1,root=0,octave=4}={}) {
  const degrees=cabineScales[scale]?.[1];if(!degrees||!Number.isInteger(row)||!Number.isInteger(col)||row<0||row>7||col<0||col>7)return -1;
  const n=degrees.length,d=(7-row)*(n===12?5:n===5?2:3)+col,pitch=octave*12+root+12*Math.floor(d/n)+degrees[d%n];
  return pitch>=0&&pitch<128?pitch:-1;
}
export const cabineNoteName=n=>`${cabineNoteNames[n%12]}${Math.floor(n/12)-2}`;
export function cabineLayout(data={}) {
  const integer=(v,min,max,fallback)=>Number.isInteger(v)?Math.min(max,Math.max(min,v)):fallback;
  return {layer:['KEYS','USER','DRUMS'].includes(data?.layer)?data.layer:'SESSION',mode:['ROTAS','SONS','GESTOS'].includes(data?.mode)?data.mode:'ROTAS',page:integer(data?.page,0,100,0),scale:integer(data?.scale,0,7,1),root:integer(data?.root,0,11,0),octave:integer(data?.octave,0,9,4),melody:!!data?.melody,drum:integer(data?.drum,0,3,0),stepPage:integer(data?.stepPage,0,3,0),solo:integer(data?.solo,-1,3,-1),mutes:Array.from({length:4},(_,i)=>!!data?.mutes?.[i])};
}
export function installCabineKeys({pads,config,note,pulse,startRow=0,onNote=()=>{}}) {
  const held=new Map(),notes=new Map();
  function paint(){pads.forEach((pad,index)=>{if(Math.floor(index/8)<startRow)return;const active=[...held.values()].some(v=>v.index===index);pad.classList.toggle('active',active);pad.setAttribute('aria-pressed',active);});}
  function start(id,index){if(held.has(id))return;const pitch=cabinePitch(Math.floor(index/8),index%8,config);if(pitch<0)return;held.set(id,{index,pitch});const count=notes.get(pitch)||0;if(!count)note(0x90,pitch,100);notes.set(pitch,count+1);pulse();onNote(pitch);paint();}
  function end(id){const voice=held.get(id);if(!voice)return;held.delete(id);const count=(notes.get(voice.pitch)||1)-1;if(count)notes.set(voice.pitch,count);else{notes.delete(voice.pitch);note(0x80,voice.pitch,0);}paint();}
  pads.forEach((pad,index)=>{if(Math.floor(index/8)<startRow)return;const pitch=cabinePitch(Math.floor(index/8),index%8,config);pad.onclick=null;pad.classList.add('cabine-key');pad.disabled=pitch<0;pad.style.touchAction='none';pad.dataset.note=pitch;pad.style.setProperty('--pad-color',(pitch-config.root)%12===0?'#c2f000':'#9a9a96');pad.textContent=pitch>=0?cabineNoteName(pitch):'—';pad.setAttribute('aria-label',pitch>=0?`${cabineNoteName(pitch)} (${pitch})`:'Fora da extensão MIDI');
    pad.onpointerdown=e=>{if(e.pointerType==='mouse'&&e.button!==0)return;e.preventDefault();pad.setPointerCapture(e.pointerId);start(e.pointerId,index);};
    const up=e=>end(e.pointerId);pad.onpointerup=pad.onpointercancel=pad.onlostpointercapture=up;
    pad.onkeydown=e=>{if((e.code==='Space'||e.code==='Enter')&&!e.repeat){e.preventDefault();start(`key-${index}`,index);}};pad.onkeyup=e=>{if(e.code==='Space'||e.code==='Enter'){e.preventDefault();end(`key-${index}`);}};
  });
  paint();return ()=>{for(const id of [...held.keys()])end(id);};
}
