// SPDX-License-Identifier: GPL-3.0-only
// plugin-style MIDI Learn / Forget by context click or stationary long press, plus an explicit mapping badge.
import {bindPopupMenu} from './popup-keyboard.js';
let dismiss=()=>{};
export function closeMidiMenu(){dismiss();}
export function mountMidiLearn({root,id,title,el,maps,waiting,learn,forget}) {
  const badge=el('button','midi-badge');badge.type='button';let timer,origin;
  function menu(){if(!root.isConnected)return;dismiss();root.cancelParameterDrag?.();const panel=el('div','midi-actions');panel.dataset.midiFor=id;panel.setAttribute('role','menu');panel.setAttribute('aria-label',`${title}: MIDI`);
    const outside=e=>{if(!panel.contains(e.target))close();},escape=e=>{if(e.key==='Escape'){e.preventDefault();e.stopPropagation();close(true);}};
    const returnTo=document.activeElement;
    function close(restore=false){panel.remove();if(restore===true&&returnTo?.isConnected)returnTo.focus({preventScroll:true});document.removeEventListener('pointerdown',outside,true);document.removeEventListener('keydown',escape,true);document.removeEventListener('scroll',close,true);window.removeEventListener('resize',close);if(dismiss===close)dismiss=()=>{};}
    for(const [name,action] of [['MIDI Learn',()=>learn(id)],['Forget',()=>forget(id)],['Fechar',()=>{}]]){const b=el('button','',name);b.type='button';b.setAttribute('role','menuitem');b.disabled=name==='Forget'&&!maps().some(m=>m.param===id);b.onclick=e=>{e.preventDefault();close(true);action();};panel.append(b);}bindPopupMenu(panel,[...panel.children],close);document.body.append(panel);
    const anchor=(root.querySelector('.dial')||root).getBoundingClientRect(),width=panel.offsetWidth,height=panel.offsetHeight;panel.style.left=`${Math.max(8,Math.min(innerWidth-width-8,anchor.left+anchor.width/2-width/2))}px`;panel.style.top=`${Math.max(8,Math.min(innerHeight-height-8,anchor.bottom+8+height<=innerHeight-8?anchor.bottom+8:anchor.top-height-8))}px`;
    dismiss=close;document.addEventListener('pointerdown',outside,true);document.addEventListener('keydown',escape,true);document.addEventListener('scroll',close,true);window.addEventListener('resize',close);panel.firstChild.focus({preventScroll:true});
  }
  badge.onclick=menu;root.append(badge);
  root.addEventListener('keydown',e=>{if(e.key==='ContextMenu'||e.key==='F10'&&e.shiftKey){e.preventDefault();menu();}});
  root.addEventListener('contextmenu',e=>{if(e.target.closest('.mod-tray,.source-bank,.mod-add'))return;e.preventDefault();menu();});
  root.addEventListener('pointerdown',e=>{if(!e.target.closest('.dial')||e.button>0)return;if(origin){cancel();return;}origin={id:e.pointerId,x:e.clientX,y:e.clientY};timer=setTimeout(()=>{timer=null;if(origin)menu();},500);});
  const cancel=()=>{if(timer)clearTimeout(timer);timer=null;origin=null;};
  root.addEventListener('pointermove',e=>{if(origin?.id===e.pointerId&&Math.hypot(e.clientX-origin.x,e.clientY-origin.y)>6)cancel();});
  root.cancelMidiLearnGesture=cancel;
  root.addEventListener('pointerup',cancel);root.addEventListener('pointercancel',cancel);root.addEventListener('lostpointercapture',cancel);
  function refresh(){const map=maps().find(m=>m.param===id);badge.hidden=!map;badge.textContent=map?`CC ${map.cc} · ${map.channel<0?'ANY CH':`CH ${map.channel+1}`}${waiting(map)?' · pickup':''}`:'';badge.setAttribute('aria-label',`${title}: ${badge.textContent}. MIDI Learn e Forget`);root.classList.toggle('has-midi',!!map);}
  refresh();return refresh;
}
