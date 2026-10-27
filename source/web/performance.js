// SPDX-License-Identifier: GPL-3.0-only
// MIDI 1 strips. Readback never echoes incoming messages to the engine or MIDI output.
export function installPerformance({note,getRange,setRange,isMpe=()=>false}){
  const pitch=document.getElementById('pitch-wheel'),mod=document.getElementById('mod-wheel'),range=document.getElementById('bend-range');
  function install(input,bipolar){
    const host=document.createElement('span');host.className=`performance-strip${bipolar?' bipolar':''}`;input.before(host);host.append(input);
    const fill=document.createElement('span'),marker=document.createElement('span');fill.className='strip-fill';marker.className='strip-marker';fill.setAttribute('aria-hidden','true');marker.setAttribute('aria-hidden','true');host.append(fill,marker);
    input.setAttribute('aria-label',bipolar?'Pitch bend':'Modulation CC1');input.setAttribute('aria-orientation','vertical');
    let owner=null;
    const paint=()=>{const min=+input.min,max=+input.max,v=+input.value,position=(v-min)/(max-min),origin=bipolar?-min/(max-min):0;host.style.setProperty('--strip-position',position);host.style.setProperty('--strip-low',Math.min(origin,position));host.style.setProperty('--strip-high',Math.max(origin,position));host.classList.toggle('engaged',owner!==null);input.setAttribute('aria-valuetext',bipolar?`${(v/8192*getRange()).toFixed(2)} semitons`:`${Math.round(v/127*100)} por cento`);};
    const emit=()=>{paint();if(bipolar){const v=+input.value+8192;note(0xe0,v&127,v>>7);}else note(0xb0,1,+input.value);};
    const move=e=>{const rect=host.getBoundingClientRect(),p=Math.max(0,Math.min(1,(rect.bottom-e.clientY)/rect.height));input.value=Math.round(+input.min+(+input.max-input.min)*p);emit();};
    input.oninput=emit;
    input.onpointerdown=e=>{if(owner!==null||e.pointerType==='mouse'&&e.button!==0)return;e.preventDefault();owner=e.pointerId;input.setPointerCapture(owner);move(e);};
    input.onpointermove=e=>{if(owner===e.pointerId){e.preventDefault();move(e);}};
    const end=e=>{if(owner!==e.pointerId||e.type==='lostpointercapture'&&input.hasPointerCapture(e.pointerId))return;if(e.type==='pointerup')move(e);const id=owner;owner=null;if(input.hasPointerCapture(id))input.releasePointerCapture(id);if(bipolar){input.value=0;emit();}else paint();};
    input.onpointerup=input.onpointercancel=end;
    input.onlostpointercapture=end;
    input.onkeyup=e=>{if(bipolar&&['ArrowUp','ArrowDown','ArrowLeft','ArrowRight','Home','End','PageUp','PageDown'].includes(e.key)){input.value=0;emit();}};
    paint();return {paint,reset(){const id=owner;owner=null;if(id!==null&&input.hasPointerCapture(id))input.releasePointerCapture(id);input.value=0;emit();}};
  }
  const bendStrip=install(pitch,true),modStrip=install(mod,false);
  range.onchange=()=>{setRange(+range.value);range.value=getRange();bendStrip.paint();};
  return {
    reset(){bendStrip.reset();modStrip.reset();},
    sync(){range.value=getRange();bendStrip.paint();modStrip.paint();},
    receiveMidi(status,a,b){if(isMpe()&&(status&15)!==0)return;const kind=status&0xf0;if(kind===0xe0){pitch.value=(a&127)+((b&127)<<7)-8192;bendStrip.paint();}else if(kind===0xb0&&a===1){mod.value=b;modStrip.paint();}else if(kind===0xb0&&a===121){pitch.value=mod.value=0;bendStrip.paint();modStrip.paint();}}
  };
}
