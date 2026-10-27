// SPDX-License-Identifier: GPL-3.0-only
import {releasePointerChoiceFocus} from './typing-keyboard.js';
import {controllerNumberText} from './mod-layout.js';
export function mountControllerPanel({root,el,state,metadata,field,descriptor,persist,learn,forget,learning,feedback,drafts,rerender}){
 const performance=el('section','controller-performance');
 for(const[id,name]of [['mpe_mode','MPE INPUT'],['bend_range','MASTER BEND'],['mpe_bend_range','NOTE BEND']]){
  const d=descriptor(id);if(d)performance.append(field({...d,name,...(id==='mpe_mode'?{choices:['OFF','LOWER ZONE / CH 1 MASTER']}:{})}));
 }root.append(performance);
 const slots=el('div','controller-slots');
 const heading=el('div','controller-heading');for(const title of ['','TYPE','CC / NOTE','CHANNEL','','',''])heading.append(el('span','',title));slots.append(heading);
 for(let i=0;i<8;i++){
  const row=el('div','controller-slot'),binding=state.controllerBindings[i],type=el('select'),number=el('select'),channel=el('select'),learnButton=el('button','controller-learn','LEARN'),forgetButton=el('button','controller-forget','Forget'),meter=el('div','controller-meter');row.dataset.controller=i;
  row.append(el('strong','',`CTRL ${i+1}`));
  for(const[v,t]of [['cc','CC'],['note','NOTE']]){const o=el('option','',t);o.value=v;type.append(o);}type.value=binding?.kind||drafts[i].kind;
  let shownKind=null;const fillNumbers=kind=>{number.replaceChildren();for(let n=-1;n<128;n++){const o=el('option','',controllerNumberText(kind,n));o.value=n;number.append(o);}shownKind=kind;};fillNumbers(type.value);number.value=binding?.number??-1;
  for(let n=-1;n<16;n++){const o=el('option','',n<0?'ANY':String(n+1));o.value=n;channel.append(o);}channel.value=binding?.channel??drafts[i].channel;
  for(const[control,label]of [[type,'Tipo'],[number,'Número'],[channel,'Canal']]){control.setAttribute('aria-label',`${label} CTRL ${i+1}`);releasePointerChoiceFocus(control);}
  const update=()=>{drafts[i]={kind:type.value,channel:+channel.value};state.controllerBindings[i]=+number.value<0?null:{kind:type.value,number:+number.value,channel:+channel.value};persist();};
  type.onchange=()=>{update();sync();};number.onchange=channel.onchange=update;
  learnButton.setAttribute('aria-label',`Learn CTRL ${i+1}`);learnButton.onclick=()=>{learn(i);sync();};forgetButton.setAttribute('aria-label',`Forget CTRL ${i+1}`);forgetButton.onclick=()=>{drafts[i]={kind:'cc',channel:-1};forget(i);sync();};
  const sync=()=>{const binding=state.controllerBindings[i],kind=binding?.kind||drafts[i].kind;if(kind!==shownKind)fillNumbers(kind);type.value=kind;number.value=binding?.number??-1;channel.value=binding?.channel??drafts[i].channel;const active=learning()===i;learnButton.setAttribute('aria-pressed',active);row.classList.toggle('learning',active);};row.dataset.readback='';row.sync=sync;sync();
  const source=metadata().sources.find(s=>s.id===`ctrl${i+1}`);if(source)feedback.mountSource(meter,source);
  row.append(type,number,channel,learnButton,forgetButton,meter);slots.append(row);
 }root.append(slots);
}
