// SPDX-License-Identifier: GPL-3.0-only
export function createTapControl({el,tap,readback,clock=()=>performance.now(),label='TAP',className='tap-control'}) {
  const button=el('button',className,'TAP');
  button.setAttribute('aria-label',label);button.dataset.readback='';
  button.sync=()=>{const text=readback(),next=text?(text.startsWith('TAP')?text:`TAP ${text}`):'TAP';if(button.textContent!==next)button.textContent=next;};
  let pointerClick=false,clearClick;
  const strike=()=>{const time=clock();button.dataset.tapAt=time;tap(time);button.sync();};
  button.onpointerdown=e=>{if(e.isPrimary===false||e.pointerType==='mouse'&&e.button!==0)return;e.preventDefault();clearTimeout(clearClick);pointerClick=true;strike();};
  button.onpointerup=button.onpointercancel=()=>{clearTimeout(clearClick);clearClick=setTimeout(()=>pointerClick=false,750);};
  button.onkeydown=()=>{pointerClick=false;};
  button.onclick=e=>{if(pointerClick){pointerClick=false;return;}if(!e?.detail)strike();};button.sync();return button;
}
