// SPDX-License-Identifier: GPL-3.0-only
// JUCE mappingsTree XML: channels are 0=ANY / 1..16; UI storage uses -1=ANY / 0..15.
import {actionBindings} from './cabine-actions.js';
const elements=n=>Array.from(n?.childNodes||[]).filter(c=>c.nodeType===1);
const esc=v=>String(v).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[c]));
export function exportMappings(state){
  const binding=b=>`type="${b?(b.kind==='cc'?0:1):-1}" num="${b?.number??-1}" ch="${b?b.channel+1:0}"`;
  return '<?xml version="1.0" encoding="UTF-8"?>\n<MAPPINGS>\n<CTRLS>'+Array.from({length:8},(_,i)=>`<CTRL ${binding(state.controllerBindings?.[i])}/>`).join('')+'</CTRLS>\n<CCMAP>'+(state.ccMaps||[]).map(m=>`<MAP id="${esc(m.param)}" cc="${m.cc}" ch="${m.channel+1}"/>`).join('')+'</CCMAP>\n<CABACT>'+actionBindings(state.actionBindings).map(b=>`<ACT ${binding(b)}/>`).join('')+'</CABACT>\n<LAUNCHPAD in="'+esc(state.mappingPorts?.input||'')+'" out="'+esc(state.mappingPorts?.output||'')+'"/>\n</MAPPINGS>\n';
}
export function importMappings(text,metadata,Parser=globalThis.DOMParser){
  if(typeof text!=='string'||text.length>262144||/<!DOCTYPE|<!ENTITY|<\?(?!xml(?:\s|\?>))/i.test(text))throw Error('Mapa XML inválido ou maior que 256 KB.');
  const doc=new Parser().parseFromString(text,'application/xml'),root=doc.documentElement;
  if(!root||root.tagName!=='MAPPINGS'||root.namespaceURI||doc.getElementsByTagName('parsererror').length)throw Error('Este arquivo não é um mapa .usinamap do Usina.');
  const groups=new Map();for(const child of elements(root)){if(!['CTRLS','CCMAP','CABACT','LAUNCHPAD'].includes(child.tagName)||groups.has(child.tagName))throw Error('Estrutura de mapa não suportada.');groups.set(child.tagName,child);}
  const number=(n,key,min,max,fallback)=>{const attr=n.getAttribute(key);if(attr===null||attr==='')return fallback;const value=Number(attr);if(!Number.isInteger(value)||value<min||value>max)throw Error(`Valor inválido no mapa: ${key}.`);return value;};
  function bindings(group,tag,count){const children=elements(groups.get(group));if(children.length>count||children.some(n=>n.tagName!==tag))throw Error('Quantidade ou tipo de vínculo inválido.');return Array.from({length:count},(_,i)=>{const n=children[i];if(!n)return null;const type=number(n,'type',-1,1,-1),num=number(n,'num',-1,127,-1),channel=number(n,'ch',0,16,0)-1;return type<0||num<0?null:{kind:type===0?'cc':'note',number:num,channel};});}
  const ids=new Set(metadata.parameters.map(d=>d.id)),ccMaps=[],children=elements(groups.get('CCMAP'));let ignored=0;
  if(children.length>64)throw Error('Limite de 64 mapas de parâmetros.');
  for(const n of children){if(n.tagName!=='MAP')throw Error('Elemento de mapa inválido.');const param=n.getAttribute('id'),cc=number(n,'cc',0,127,0),channel=number(n,'ch',0,16,0)-1;if(!ids.has(param)){++ignored;continue;}ccMaps.push({param,cc,channel});}
  const ports=groups.get('LAUNCHPAD'),input=ports?.getAttribute('in')||'',output=ports?.getAttribute('out')||'';if(input.length>256||output.length>256)throw Error('Nome de porta muito longo.');
  return {controllerBindings:bindings('CTRLS','CTRL',8),actionBindings:bindings('CABACT','ACT',16),ccMaps,mappingPorts:{input,output},ignored};
}
