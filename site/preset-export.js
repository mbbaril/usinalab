// SPDX-License-Identifier: GPL-3.0-only
// Sound-only XML for the current plugin loader. No DSP or live-state mutations.
const RIG=new Set(['in_gain','in_mix','in_fx','feedback','fb_time','level','ceiling','oversampling','seq_bpm','seq_play','bend_range','mpe_mode','mpe_bend_range']);
const nativeOnly={seq_on:0,ir_on:0,ir_mix:1,ir_lowcut:20,ir_highcut:20000,ir_out:0};
const escape=value=>String(value).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&apos;');
export function soundFilename(name){const clean=String(name).normalize('NFC').replace(/[<>:"/\\|?*\u0000-\u001f]/g,' ').replace(/[. ]+$/g,'').trim().slice(0,70)||'Usina';return `${/^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)/i.test(clean)?'Usina '+clean:clean}.usina`;}
export function exportPluginSound(state,metadata,name=state?.preset){
 if(!state||!metadata?.parameters?.length)throw Error('Timbre indisponível.');
 name=typeof name==='string'?name.trim():'';
 if(!name||name.length>120||/[\u0000-\u001f\u007f-\u009f\uD800-\uDFFF]/u.test(name))throw Error('Use um nome de timbre válido, com até 120 caracteres.');
 const parameters=new Map(metadata.parameters.map(d=>[d.id,d]));
 for(const id of Object.keys(state.params||{}))if(!parameters.has(id))throw Error(`Parâmetro não suportado: ${id}.`);
 const values=[];
 for(const d of metadata.parameters){
  if(RIG.has(d.id))continue;
  const value=state.params?.[d.id]??d.default;
  if(typeof value!=='number'||!Number.isFinite(value)||value<d.min-1e-6||value>d.max+1e-6||(d.step===1&&Math.abs(value-Math.round(value))>1e-4))throw Error(`Valor inválido: ${d.name}.`);
  values.push([d.id,Math.min(d.max,Math.max(d.min,d.min+Math.round((value-d.min)/d.step)*d.step))]);
 }
 values.push(...Object.entries(nativeOnly));
 const routes=state.routes??[],functions=state.functions;
 if(!Array.isArray(routes)||routes.length>64)throw Error('O timbre aceita até 64 rotas.');
 const sources=new Map(metadata.sources.map(s=>[s.index,s.id])),destinations=new Map(metadata.destinations.map(d=>[d.index,d.name]));
 const routeXml=routes.map(r=>{const src=sources.get(r.source),dst=destinations.get(r.destination);if(!src||!dst||dst.startsWith('IR ')||typeof r.amount!=='number'||!Number.isFinite(r.amount)||Math.abs(r.amount)>1)throw Error('Rota de modulação não suportada.');return `    <ROUTE src="${escape(src)}" dst="${escape(dst)}" amount="${r.amount}"/>`;});
 if(!Array.isArray(functions)||functions.length!==3)throw Error('O timbre precisa de três curvas FUNC.');
 const functionXml=functions.map((f,k)=>{if(!Array.isArray(f)||f.length<2||f.length>64||f.some((p,i)=>!Array.isArray(p)||p.length!==3||p.some(v=>typeof v!=='number'||!Number.isFinite(v))||p[0]<0||p[0]>1||p[1]<0||p[1]>1||Math.abs(p[2])>1||i>0&&p[0]<f[i-1][0]))throw Error(`Curva inválida em FUNC ${k+1}.`);return `    <FUNC points="${f.map(p=>`${p[0].toFixed(5)} ${p[1].toFixed(5)} ${p[2].toFixed(4)}`).join(';')}"/>`;});
 const text=`<?xml version="1.0" encoding="UTF-8"?>\n<USINA stateVersion="6" usinaPreset="1" userPreset="${escape(name)}" irFile="">\n${values.map(([id,value])=>`  <PARAM id="${id}" value="${value}"/>`).join('\n')}\n  <MODS>\n${routeXml.join('\n')}\n  </MODS>\n  <FUNCS>\n${functionXml.join('\n')}\n  </FUNCS>\n  <SEQ res="4" swing="0"/>\n</USINA>\n`;
 return {text,name,filename:soundFilename(name),parameters:values.length,routes:routes.length,functions:3,irBypassed:values.some(([id,value])=>/^(fx_[ab][23]?|aux[23]?|loop[23]?)_type$/.test(id)&&value===4)};
}
