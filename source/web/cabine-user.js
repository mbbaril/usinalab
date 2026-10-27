// SPDX-License-Identifier: GPL-3.0-only
// Direct port of Cabine.cpp userPadAt, userPress and tap. Values on momentary pads are normalized.
const colors={lfo:'#25e0c4',fx:'#9d4edd',macro:'#ff5ca8',filter:'#c2f000',reso:'#25e0c4',aux:'#3fa2ff',white:'#e8e8e3'};
const moment=(ids,value,name,color)=>({ids,value,name,color});
const action=(name,run,color)=>({name,run,color});
export function userPadAt(index){
  const row=Math.floor(index/8),col=index%8;
  if(row===0)return [action('TAP LFO 1','tap0',colors.lfo),action('TAP LFO 2','tap1',colors.lfo),action('TAP LFO 3','tap2',colors.lfo),action('TAP DELAY','tap3',colors.fx),null,action('PRESET <','previous',colors.white),action('PRESET >','next',colors.white),action('INIT','init',colors.white)][col];
  if(row===1)return [action('FREEZE DELAY','delayFreeze',colors.fx),action('FREEZE VAPOR','vaporFreeze',colors.fx),moment(['tex_freeze'],1,'FREEZE TEXTURE',colors.fx),null,moment(['cutoff'],.08,'CUTOFF DOWN',colors.filter),moment(['cutoff'],1,'CUTOFF OPEN',colors.filter),moment(['reso'],.85,'RESO UP',colors.reso),null][col];
  if(row>=2&&row<=5)return moment([`macro${row-1}`],(col+1)/8,`MACRO ${row-1} ${(col+1)*12.5}%`,colors.macro);
  if(row===6)return [moment(['fx_a_mix','fx_b_mix'],0,'EFFECTS DRY',colors.fx),moment(['fx_a_mix','fx_b_mix'],1,'EFFECTS WET',colors.fx),moment(['aux_send'],1,'AUX THROW',colors.aux),moment(['dly_feedback'],.86,'DELAY FEEDBACK UP',colors.fx),moment(['dly_mix'],1,'DELAY WASH',colors.fx),null,null,null][col];
  return null;
}
// FmSchema.h kSyncBeats, including its unused FREE entry.
const divisions=[0,.125,1/6,.25,.375,1/3,.5,.75,2/3,1,1.5,2,4,8,16];
export function createUserPerformance({normalized,setNormalized,param,setParam,library,choosePreset,state}){
  const held=new Map(),taps=Array.from({length:4},()=>[]),tapText=Array(4).fill('');
  function tap(target,time){
    const history=taps[target];if(history.length&&time-history.at(-1)>2000)history.length=0;history.push(time);if(history.length>4)history.shift();
    if(history.length<2){tapText[target]='TAP…';return;}const period=(history.at(-1)-history[0])/(history.length-1);if(period<=0)return;
    const prefix=target===3?'dly':`lfo${target+1}`,sync=`${prefix}_sync`;
    if(param(sync)>0){const beats=period*param('seq_bpm')/60000;let best=9,error=Infinity;for(let i=1;i<15;++i){const next=Math.abs(Math.log(divisions[i]/beats));if(next<error){error=next;best=i;}}setParam(sync,best);tapText[target]=['FREE','1/32','1/16T','1/16','1/16D','1/8T','1/8','1/8D','1/4T','1/4','1/4D','1/2','1 BAR','2 BARS','4 BARS'][best];}
    else if(target===3){const ms=Math.max(1,Math.min(4000,period));setParam('dly_time',ms);tapText[target]=`${Math.round(ms)} ms`;}
    else {const speed=Math.max(0,Math.min(127,Math.round(127*Math.log10(1000/period/.01)/4)));setParam(`${prefix}_speed`,speed);tapText[target]=`${(.01*10000**(speed/127)).toFixed(2)} Hz`;}
  }
  function run(pad,down,time){
    if(pad.run==='delayFreeze'||pad.run==='vaporFreeze'){setParam(pad.run==='delayFreeze'?'dly_freeze':'vap_freeze',down?1:0);return;}
    if(!down)return;if(pad.run.startsWith('tap')){tap(+pad.run.slice(3),time);return;}
    const list=library();if(!list.length)return;const index=Math.max(0,list.findIndex(p=>p.name===state.preset));
    const next=pad.run==='init'?list.find(p=>p.name==='INIT'):list[(index+(pad.run==='next'?1:list.length-1))%list.length];if(next)choosePreset(next);
  }
  function press(index,time=performance.now()){
    const pad=userPadAt(index);if(!pad||held.has(index))return;
    if(pad.run){held.set(index,{pad});run(pad,true,time);return;}
    const others=[...held.entries()].filter(([,v])=>v.pad.ids?.[0]===pad.ids[0]).sort((a,b)=>a[0]-b[0]);
    const previous=others.length?others.at(-1)[1].previous:pad.ids.map(normalized);held.set(index,{pad,previous});pad.ids.forEach(id=>setNormalized(id,pad.value));
  }
  function release(index){
    const record=held.get(index);if(!record)return;held.delete(index);const {pad,previous}=record;
    if(pad.run){run(pad,false,0);return;}
    const other=[...held.entries()].filter(([,v])=>v.pad.ids?.[0]===pad.ids[0]).sort((a,b)=>a[0]-b[0]).at(-1)?.[1];
    pad.ids.forEach((id,k)=>setNormalized(id,other?other.pad.value:previous[k]));
  }
  return {press,release,held,tapText,tap,cancel(){for(const index of [...held.keys()])release(index);}};
}
export function renderUserPads({root,el,performance,pulse}){
  root.append(el('p','perform-note','USER do plugin: TAPs e presets por toque; freezes, macros e throws enquanto segurados. Ao soltar, os controles momentâneos voltam ao valor anterior.'));
  const headers=el('div','cabine-headers');for(let i=0;i<8;++i)headers.append(el('span','',String(i+1)));root.append(headers);
  const grid=el('div','cabine-grid');grid.setAttribute('aria-label','CABINE USER');const pointers=new Map(),owners=new Map(),pads=[];
  function refresh(){pads.forEach((pad,i)=>{const on=performance.held.has(i);pad.classList.toggle('active',on);pad.setAttribute('aria-pressed',on);const spec=userPadAt(i);if(i<4&&spec)pad.textContent=spec.name+(performance.tapText[i]?` · ${performance.tapText[i]}`:'');});}
  function end(id){const index=pointers.get(id);if(index===undefined)return;pointers.delete(id);owners.delete(index);performance.release(index);refresh();}
  for(let i=0;i<64;++i){const spec=userPadAt(i),pad=el('button','cabine-pad cabine-user',spec?.name||'—');pad.dataset.pad=i;pad.disabled=!spec;pad.style.touchAction='none';pad.style.setProperty('--pad-color',spec?.color||'#292929');pad.setAttribute('aria-label',spec?.name||'Vazio');
    pad.onpointerdown=e=>{if(e.pointerType==='mouse'&&e.button!==0||owners.has(i))return;e.preventDefault();pad.setPointerCapture(e.pointerId);pointers.set(e.pointerId,i);owners.set(i,e.pointerId);pulse();performance.press(i);refresh();};
    pad.onpointerup=pad.onpointercancel=pad.onlostpointercapture=e=>end(e.pointerId);
    pad.onkeydown=e=>{if((e.code==='Space'||e.code==='Enter')&&!e.repeat){e.preventDefault();if(owners.has(i))return;pointers.set(`key-${i}`,i);owners.set(i,`key-${i}`);pulse();performance.press(i);refresh();}};
    pad.onkeyup=e=>{if(e.code==='Space'||e.code==='Enter'){e.preventDefault();end(`key-${i}`);}};pads.push(pad);grid.append(pad);
  }
  root.append(grid);root.refreshCabine=refresh;root.cancelGestures=()=>{pointers.clear();owners.clear();performance.cancel();refresh();};refresh();
}
