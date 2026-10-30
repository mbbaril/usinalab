// SPDX-License-Identifier: GPL-3.0-only
// Slot order, power IDs and editor focus follow plugin/ui/FxPanel.cpp.
import {createFxSlotActions} from './fx-slot-actions.js';
import {fxChoiceParameter} from './fx-choices.js';
import {renderTextureLayout} from './texture-layout.js';
import {fxBusFields} from './fx-bus-layout.js';
import {installFxSlotDrag} from './fx-slot-drag.js';
import {fxValueFormatter} from './fx-values.js';
import {createTapControl} from './tap-control.js';
import {renderFxEditorLayout} from './fx-editor-layout.js';
export const fxBuses=['FX A','FX B','AUX','LOOP'];
export const fxPower=['','tex_on','rev_on','dist_on','ir_on','vap_on','dly_on','fxf_on'];
const linearFilterTypes=new Set([0,1,4,5,7,8,9,10]);  // SVF types: DRIVE has no effect
const vaporIds=['position','size','pitch','density','texture','drywet','spread','feedback','reverb'];
export const distortionCharacterNames=['EVEN','ASYM','ASYM','STARVE','EDGE','FULL','CRUSH','MASK','BITS'];
export function distortionCharacterText(mode,value){const k=Math.max(0,Math.min(1,value));if(mode===6)return `/${1+Math.floor(k*k*63)} ${(16-12*k).toFixed(1)} BITS`;if(mode===7)return `${1+Math.floor(k*14)} BITS`;if(mode===8)return `${8-Math.floor(k*6)} BITS`;if(mode===5){if(k<.005)return 'HALF';if(k>.995)return 'FULL';return `${Math.round(k*100)} % FULL`;}return `${Math.round(k*100)} %`;}
export function swapFxSlots(from,to,param,setParam){if(from===to)return false;const a=param(from),b=param(to);setParam(from,b);setParam(to,a);return true;}
export function fxEditorFields(descriptors,kind,effects,metadata,mode,param=()=>0){return descriptors.filter(d=>d.group===effects[kind]).map(original=>{
  const formatter=fxValueFormatter(original.id),d=fxChoiceParameter({...original,...(formatter?{text:formatter}:{})});
  if(kind===5&&vaporIds.includes(d.id.slice(4)))return {...d,name:metadata.vaporKnobs[mode][vaporIds.indexOf(d.id.slice(4))]};
  if(kind===3&&d.id==='dist_char'){const m=param('dist_mode');return {...d,uiCaption:distortionCharacterNames[m],text:v=>distortionCharacterText(m,v)};}
  if(kind===6&&['dly_time','dly_wow','dly_flutter'].includes(d.id))return {...d,dim:()=>d.id==='dly_time'?param('dly_sync')>0:param('dly_character')===0};
  if(kind===7&&d.id==='fxf_drive')return {...d,dim:()=>linearFilterTypes.has(param('fxf_type'))};
  return d;
});}
export function createFxPanel({el,param,setParam,descriptors,metadata,field,section,subtabs,slots,effects,rerender,tapDelay=null,tapText=()=>'',clock=()=>performance.now()}){
  let bus=0,actionMessage='';const focus=[0,0,0,0];
  function render(root){const sectionFx=(host,title,items)=>section(host,title,items.map(d=>({...d,text:d.text??fxValueFormatter(d.id)??undefined,compactToggle:true})));root.classList.add('fx-view');root.refreshFx=null;const cancellations=[];root.cancelGestures=()=>cancellations.forEach(f=>f());const all=descriptors(),group=fxBuses[bus],slotDescriptors=slots.map(id=>all.find(d=>d.id===id)).filter(d=>d?.group===group);
    const routing=sectionFx(el('div'),'ROUTING',fxBusFields(all,'ROUTING',param));routing.classList.add('fx-routing');
    root.append(subtabs(fxBuses,bus,i=>{bus=i;rerender();}));
    const board=el('div','fx-board'),rack=el('aside','fx-rack'),editor=el('section','fx-editor');root.append(board);board.append(rack,editor);rack.append(routing);
    // The desktop exposes all buses at once; reveal their slot targets during a mobile drag.
    const dropGrid=el('div','fx-drop-grid');dropGrid.hidden=true;dropGrid.setAttribute('aria-label','Destinos dos efeitos');board.append(dropGrid);
    fxBuses.forEach(name=>{const column=el('section','fx-drop-bus');column.append(el('h4','',name));slots.filter(id=>all.find(d=>d.id===id)?.group===name).forEach((id,index)=>{const target=el('div','fx-drop-slot',`${index+1} · ${effects[param(id)]}`);target.dataset.fxDrop=id;column.append(target);});dropGrid.append(column);});
    const clearDrop=()=>board.querySelectorAll('.drop-target').forEach(r=>r.classList.remove('drop-target'));
    const busControls=sectionFx(rack,group==='LOOP'?'LOOP INSERT':group,fxBusFields(all,group,param));busControls.classList.add('bus-controls');
    slotDescriptors.forEach((d,i)=>{const row=el('div','fx-slot-row'),choose=el('button','fx-slot-focus',String(i+1)),type=field(d),kind=param(d.id);row.dataset.fxSlot=d.id;row.classList.toggle('selected',focus[bus]===i);choose.setAttribute('aria-label',`Editar ${group} slot ${i+1}`);choose.setAttribute('aria-pressed',focus[bus]===i);
      cancellations.push(installFxSlotDrag({handle:choose,row,onSelect:()=>{focus[bus]=i;rerender();},onStart:()=>{const r=editor.getBoundingClientRect();dropGrid.style.left=`${r.left+4}px`;dropGrid.style.right=`${Math.max(8,innerWidth-r.right+4)}px`;dropGrid.style.top=`${Math.max(48,Math.min(r.top+30,innerHeight-220))}px`;dropGrid.hidden=false;},onMove:e=>{clearDrop();const hit=document.elementFromPoint(e.clientX,e.clientY)?.closest('[data-fx-drop],.fx-slot-row');if(!hit||!board.contains(hit))return null;const target=hit.dataset.fxDrop||hit.dataset.fxSlot;hit.classList.add('drop-target');return target;},onEnd:()=>{dropGrid.hidden=true;clearDrop();},onDrop:target=>{if(swapFxSlots(d.id,target,param,setParam)){const descriptor=all.find(p=>p.id===target);bus=fxBuses.indexOf(descriptor.group);focus[bus]=slots.filter(id=>all.find(p=>p.id===id)?.group===descriptor.group).indexOf(target);}rerender();}}));
      type.addEventListener('change',()=>{focus[bus]=i;},true);row.append(choose,type);
      const power=all.find(p=>p.id===fxPower[kind]);if(power&&kind!==4){const p=field({...power,compactToggle:true,toggleLabel:'ON'});p.classList.add('slot-power');row.append(p);}else{const off=el('button','','ON');off.disabled=true;row.append(off);}rack.append(row);
    });
    rack.append(busControls);
    const selected=slotDescriptors[focus[bus]]||slotDescriptors[0];if(!selected)return;
    const targets=slots.map(id=>{const d=all.find(p=>p.id===id),siblings=slots.filter(other=>all.find(p=>p.id===other)?.group===d.group);return {id,label:`${d.group} slot ${siblings.indexOf(id)+1} · ${effects[param(id)]}`};});
    rack.append(createFxSlotActions({el,slots:targets,selected:selected.id,param,exchange:(from,to,action)=>{if(!swapFxSlots(from,to,param,setParam))return;const d=all.find(p=>p.id===to);bus=fxBuses.indexOf(d.group);focus[bus]=slots.filter(id=>all.find(p=>p.id===id)?.group===d.group).indexOf(to);actionMessage=`${action==='move'?'Efeito movido':'Slots trocados'}: ${targets.find(s=>s.id===from).label} → ${targets.find(s=>s.id===to).label}.`;rerender();requestAnimationFrame(()=>{if(document.activeElement===document.body||root.contains(document.activeElement))root.querySelector(`[data-fx-slot="${to}"] .fx-slot-focus`)?.focus();});}}));
    const result=el('p','fx-slot-result',actionMessage);result.setAttribute('role','status');rack.append(result);const kind=param(selected.id);editor.dataset.fxEditor=selected.id;editor.append(el('h3','',`${group} · ${focus[bus]+1} / ${effects[kind]}`));
    if(!kind){editor.append(el('p','perform-note','Escolha um efeito para este slot.'));return;}
    if(kind===4){editor.append(el('p','perform-note','Convolução IR ainda indisponível no APK.'));return;}
    let tap=null;if(kind===6&&tapDelay){tap=createTapControl({el,tap:tapDelay,readback:tapText,clock,label:'TAP do delay',className:'delay-tap'});root.refreshFx=tap.sync;}
    const fields=fxEditorFields(all,kind,effects,metadata(),param('vap_mode'),param);
    if(kind===1)renderTextureLayout({root:editor,el,fields,field,section:sectionFx});else if(!renderFxEditorLayout({root:editor,el,kind,fields,field,tap}))sectionFx(editor,effects[kind],fields);
  }
  return {render};
}
