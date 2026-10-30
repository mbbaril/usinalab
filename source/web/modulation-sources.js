// SPDX-License-Identifier: GPL-3.0-only
import {sourceColor} from './controls.js';
let closePicker=()=>{};
export function closeSourcePicker(){closePicker();}
export function openSourcePicker({anchor,el,metadata,pick}){
  closePicker();const panel=sourceBank({el,metadata,pick:source=>{close(true);pick(source);}});panel.classList.add('local-source-bank','source-popover');panel.setAttribute('role','dialog');panel.setAttribute('aria-label','Escolher fonte de modulação');panel.setAttribute('aria-modal','false');document.body.append(panel);
  const rect=anchor.getBoundingClientRect(),height=panel.offsetHeight,width=panel.offsetWidth;panel.style.left=`${Math.max(8,Math.min(innerWidth-width-8,rect.left+rect.width/2-width/2))}px`;panel.style.top=`${Math.max(8,Math.min(innerHeight-height-8,rect.bottom+8))}px`;
  const outside=e=>{if(!panel.contains(e.target)&&!anchor.contains(e.target))close();},key=e=>{if(e.key==='Escape'){e.preventDefault();e.stopPropagation();close(true);}};
  function close(restore=false){panel.remove();if(restore===true&&anchor.isConnected)anchor.focus({preventScroll:true});document.removeEventListener('pointerdown',outside,true);document.removeEventListener('keydown',key,true);window.removeEventListener('resize',close);if(closePicker===close)closePicker=()=>{};}
  closePicker=close;document.addEventListener('pointerdown',outside,true);document.addEventListener('keydown',key,true);window.addEventListener('resize',close);panel.addEventListener('focusout',e=>{if(e.relatedTarget&&!panel.contains(e.relatedTarget)&&e.relatedTarget!==anchor)close();});panel.querySelector('.source-chip')?.focus();
}
const families=[['Teclado',s=>['vel','at','mw','key','expr','timbre'].includes(s.id)],['Envelopes',s=>/^(env|openv)/.test(s.id)],['LFO',s=>s.id.startsWith('lfo')],['Funções',s=>s.id.startsWith('func')],['Acaso',s=>s.id.startsWith('rand')],['Comb',s=>s.id.startsWith('comb')],['Macros',s=>s.id.startsWith('mac')],['Seq',s=>s.id.startsWith('seq')],['CTRL',s=>s.id.startsWith('ctrl')]];
export function sourceBank({el,metadata,pick}) {
  const root=el('div','source-bank');let family=2;
  function render(){const nav=el('div','source-families'),chips=el('div','source-chips');families.forEach(([name],i)=>{const b=el('button',i===family?'active':'',name);b.type='button';b.setAttribute('aria-pressed',i===family);b.onclick=e=>{e.preventDefault();family=i;render();root.querySelector('.source-families').children[i]?.focus();};nav.append(b);});
    metadata().sources.filter(families[family][1]).forEach(s=>{const b=el('button','source-chip',s.name);b.type='button';b.dataset.source=s.index;b.style.setProperty('--source-color',sourceColor(s.id));b.onclick=e=>{e.preventDefault();pick(s.index);};chips.append(b);});root.replaceChildren(nav,chips);
  }render();return root;
}
