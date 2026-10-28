// SPDX-License-Identifier: GPL-3.0-only
// Session layer faithfully follows src/plugin/Cabine.cpp. Launchpad hardware layers remain separate work.
import {cabineSurface,selectCabineSurface,createCabineNavigation} from './cabine-navigation.js';
import {installMatrixViewport} from './matrix-viewport.js';
import { sourceColor } from './controls.js';
import { tactilePulse } from './haptics.js';
import { installCabineGestures } from './cabine-gestures.js';
import { cabineScales, cabineNoteNames, cabineLayout, installCabineKeys } from './cabine-keys.js';
import { createUserPerformance, renderUserPads } from './cabine-user.js';
import { createCabineSequence, renderCabineSequence } from './cabine-sequence.js';
const sourceIds = ['lfo1', 'lfo2', 'env1', 'rand1', 'vel', 'mac1', 'mac2'];
const pages = [
  ['CUTOFF','RESO','FLT DRIVE','OP1.LEVEL','OP2.LEVEL','OP3.LEVEL','OP4.LEVEL','OP5.LEVEL'],
  ['DLY TIME','DLY FEEDBACK','DLY MIX','VAP POSITION','VAP SIZE','VAP DRY/WET','DIST DRIVE','TEX DRY/WET']
];
const gestures = [['macro1'],['macro2'],['macro3'],['macro4'],['cutoff'],['reso'],['fx_a_mix','fx_b_mix'],['aux_send']];
const names = ['MAC 1','MAC 2','MAC 3','MAC 4','CUTOFF','RESO','EFEITOS','ESPAÇO'];
// Category order and palette from PresetLibrary::categories / Cabine.cpp kCatCol.
const categoryOrder=['init','show','lead','bass','pad','drone','inst','kick','snare','hat','metal','noise','body','friction','machine','bowed','texture','extreme','filter','fx'];
const categoryColors=['#e8e8e3','#c2f000','#25e0c4','#9d4edd','#ffb000','#3fa2ff','#ff5ca8','#ff7a45'];
export function createCabine({ el, state, metadata, param, descriptors, setParam, send, note, library, choosePreset, message, cursors, changed=()=>{} }) {
  let mode = 'ROTAS', page = 0, haptics = false;
  const normalized = id => { const d = descriptors().find(p => p.id === id); return Math.pow((param(id) - d.min) / (d.max - d.min), d.skew || 1); };
  const setNormalized = (id, value) => { const d = descriptors().find(p => p.id === id); setParam(id, d.min + (d.max - d.min) * Math.pow(value, 1 / (d.skew || 1))); };
  const userPerformance=createUserPerformance({normalized,setNormalized,param,setParam,library,choosePreset,state});
  const sequenceModel=createCabineSequence({state,send,param,setParam});
  function render(root) {
    root.cancelGestures?.();
    root.classList.remove('cabine-sequencing');root.resetMatrixViewport=null;
    state.cabine=cabineLayout(state.cabine);const config=state.cabine;mode=config.mode;page=config.page;
    const rerender=()=>{root.cancelGestures?.();root.replaceChildren();render(root);};
    const meta = metadata(), toolbar = el('div','cabine-toolbar'), tools=el('div','subtabs cabine-tools');
    root.dataset.cabineSurface=cabineSurface(config);
    const navigation=createCabineNavigation({el,config,select:surface=>{
      if(!selectCabineSurface(config,surface))return;
      rerender();root.querySelector(`[data-cabine-surface="${surface}"]`)?.focus({preventScroll:true});
    }});
    toolbar.append(navigation,tools);
    const maps=el('button','','MAPAS');maps.onclick=()=>document.querySelector('[data-view=connections]').click();tools.append(maps);
    if(config.layer==='KEYS'){
      for(const [key,choices] of [['root',cabineNoteNames],['scale',cabineScales.map(s=>s[0])]]){const select=el('select');select.setAttribute('aria-label',key==='root'?'Tônica da CABINE':'Escala da CABINE');choices.forEach((name,i)=>{const o=el('option','',name);o.value=i;select.append(o);});select.value=config[key];select.onchange=()=>{config[key]=+select.value;rerender();};tools.append(select);}
      const release=el('button','','Soltar notas');release.onclick=()=>root.cancelGestures?.();tools.append(release);
    }
    const tactile = el('button', haptics ? 'active' : '', haptics ? 'Tátil ●' : 'Tátil ○'); tactile.disabled=!window.Capacitor?.isNativePlatform(); tactile.title='Feedback tátil no APK Android, respeitando os ajustes do aparelho.'; tactile.setAttribute('aria-pressed',haptics); tactile.onclick = () => { haptics = !haptics; tactile.textContent = haptics ? 'Tátil ●' : 'Tátil ○'; tactile.classList.toggle('active',haptics); tactile.setAttribute('aria-pressed',haptics); if(haptics)tactilePulse(); }; tools.append(tactile);
    const keys=config.layer==='KEYS',maxPage = keys?9:config.layer!=='SESSION'?0:mode === 'ROTAS' ? 1 : mode === 'SONS' ? Math.max(0,Math.ceil(library().length / 56)-1) : 0,currentPage=keys?config.octave:config.layer!=='SESSION'?0:Math.min(page,maxPage);page=currentPage;if(config.layer==='SESSION')config.page=page;
    if(keys||config.layer==='SESSION'&&mode!=='GESTOS')for (const [label, delta] of [['◀',-1],['▶',1]]) { const b = el('button','',label); b.setAttribute('aria-label',keys?(delta<0?'Oitava anterior da CABINE':'Próxima oitava da CABINE'):(delta < 0 ? 'Página anterior da CABINE' : 'Próxima página da CABINE')); b.disabled = currentPage + delta < 0 || currentPage + delta > maxPage; b.onclick = () => { config[keys?'octave':'page']=currentPage+delta;rerender(); }; tools.append(b); }
    const expand=el('button','cabine-expand',document.documentElement.classList.contains('cabine-expanded')?'Voltar':'Ampliar');expand.setAttribute('aria-label','Ampliar ou reduzir a CABINE');expand.setAttribute('aria-pressed',document.documentElement.classList.contains('cabine-expanded'));expand.onclick=()=>{root.cancelGestures?.();const on=document.documentElement.classList.toggle('cabine-expanded');expand.textContent=on?'Voltar':'Ampliar';expand.setAttribute('aria-pressed',on);root.resetMatrixViewport?.();};tools.append(expand);
    if(config.layer==='USER'){root.append(toolbar);renderUserPads({root,el,performance:userPerformance,pulse:()=>{if(haptics)tactilePulse();}});return;}
    if(config.layer==='DRUMS'){renderCabineSequence({root,toolbar,el,state,model:sequenceModel,param,note,pulse:()=>{if(haptics)tactilePulse();},rerender,cursors});return;}
    if(keys){
      root.append(toolbar,el('p','perform-note','KEYS: disposição isomórfica do plugin. Cada coluna avança um grau; as linhas sobem uma quarta. Solte para encerrar a nota.'));
      const headers=el('div','cabine-headers');for(let i=0;i<8;++i)headers.append(el('span','',`${i+1}`));root.append(headers);
      const grid=el('div','cabine-grid');grid.setAttribute('aria-label','CABINE KEYS');const pads=Array.from({length:64},(_,i)=>{const p=el('button','cabine-pad');p.dataset.pad=i;grid.append(p);return p;});root.append(grid);root.cancelGestures=installCabineKeys({pads,config,note,pulse:()=>{if(haptics)tactilePulse();}});root.refreshCabine=null;return;
    }
    root.append(toolbar, el('p','perform-note',mode === 'ROTAS' ? `Página ${page+1} · fontes nas linhas, destinos nas colunas. OFF → +50% → +100% → −50% → OFF.` : mode === 'SONS' ? `Página ${page+1} · 56 sons por página. A faixa inferior controla os macros.` : 'Oito faders por toque. A faixa inferior controla MACRO 1 e 2.'));
    const headers = el('div','cabine-headers');
    const captions = mode === 'ROTAS' ? pages[page] : mode === 'GESTOS' ? names : Array.from({length:8},(_,i)=>String(i+1));
    captions.forEach(name=>headers.append(el('span','',name))); root.append(headers);
    const grid = el('div','cabine-grid'); grid.setAttribute('aria-label',`CABINE ${mode}`);
    const pads = [];
    function refresh() {
      pads.forEach((pad, index) => {
        const row = Math.floor(index/8), col = index%8;
        let active = false, depth = 0;
        if(row===7) { const id=col<4?'macro1':'macro2',level=Math.round(normalized(id)*4),k=col%4;active=k<level;depth=active?(k===level-1?1:.45):0; }
        else if(mode==='ROTAS') { const src=meta.sources.find(s=>s.id===sourceIds[row]), dst=meta.destinations.find(d=>d.name===pages[page][col]); const route=state.routes.find(r=>r.source===src?.index&&r.destination===dst?.index); depth=route?.amount||0; active=!!route; pad.textContent=route?(depth>0?'+':'')+Math.round(depth*100)+'%':'·'; pad.setAttribute('aria-pressed',active); }
        else if(mode==='GESTOS') { const level=Math.round(normalized(gestures[col][0])*6),fromBottom=6-row;active=fromBottom<=level;depth=active?(fromBottom===level?1:.45):0; }
        else {const p=library()[page*56+index];active=p?.name===state.preset;pad.style.setProperty('--pad-color',active?'#e8e8e3':categoryColors[Math.max(0,categoryOrder.indexOf(p?.cat))%categoryColors.length]);}
        pad.classList.toggle('active',active); pad.classList.toggle('negative',depth<0); pad.style.setProperty('--strength',Math.max(.3,Math.abs(depth)));
        pad.setAttribute('aria-pressed',active);
      });
    }
    function activate(index,dragged=false) {
      const row=Math.floor(index/8),col=index%8;
      if(haptics)tactilePulse();
      if(row===7) { const id=col<4?'macro1':'macro2',value=(col%4+1)/4; setNormalized(id,!dragged&&Math.abs(normalized(id)-value)<.01?0:value); }
      else if(mode==='ROTAS') {
        const source=meta.sources.find(s=>s.id===sourceIds[row])?.index,destination=meta.destinations.find(d=>d.name===pages[page][col])?.index;
        if(source===undefined||destination===undefined)return;
        const i=state.routes.findIndex(r=>r.source===source&&r.destination===destination);
        if(i<0) { if(state.routes.length>=64){message('Limite de 64 rotas atingido.',true);return;} const r={source,destination,amount:.5};state.routes.push(r);send({type:'route',index:state.routes.length-1,...r}); }
        else { const r=state.routes[i];if(r.amount<=0){state.routes.splice(i,1);send({type:'route',index:i,source:0});}else{r.amount=r.amount>.75?-.5:1;send({type:'route',index:i,...r});} }
      } else if(mode==='GESTOS') gestures[col].forEach(id=>setNormalized(id,(6-row)/6));
      else { const p=library()[page*56+index];if(p)choosePreset(p); }
      refresh();
    }
    for(let index=0;index<64;++index) {
      const row=Math.floor(index/8),col=index%8,pad=el('button','cabine-pad'); pad.dataset.pad=index;
      const src=meta.sources.find(s=>s.id===sourceIds[row]);
      if(col===0&&mode==='ROTAS'&&row<7)pad.dataset.source=src?.name||'';
      pad.style.setProperty('--pad-color',row===7?(col<4?'#ff5ca8':'#3fa2ff'):mode==='ROTAS'?sourceColor(src?.id):mode==='GESTOS'?['#ff5ca8','#ff5ca8','#ff5ca8','#ff5ca8','#c2f000','#25e0c4','#9d4edd','#3fa2ff'][col]:'#c2f000');
      if(row===7) {pad.textContent=`M${col<4?1:2} ${(col%4+1)*25}`;pad.setAttribute('aria-label',`Macro ${col<4?1:2} ${(col%4+1)*25} por cento normalizado`);}
      else if(mode==='ROTAS')pad.setAttribute('aria-label',`${src?.name} para ${pages[page][col]}`);
      else if(mode==='GESTOS'){pad.textContent=String(Math.round((6-row)/6*100));pad.setAttribute('aria-label',`${names[col]} ${pad.textContent} por cento normalizado`);}
      else {const p=library()[page*56+index];pad.textContent=p?.name||'—';pad.disabled=!p;pad.setAttribute('aria-label',p?.name||'Vazio');}
      pad.onclick=()=>activate(index);pads.push(pad);grid.append(pad);
    }
    root.append(grid);root.refreshCabine=refresh;const cancelPads=installCabineGestures({grid,pads,mode,activate});let viewport=null;
    if(mode==='ROTAS'){
      const frame=el('div','matrix-viewport'),headerFrame=el('div','matrix-header-viewport');root.insertBefore(headerFrame,headers);headerFrame.append(headers);root.insertBefore(frame,grid);frame.append(grid);const labels=sourceIds.map(id=>{const label=el('span','matrix-source-label',meta.sources.find(s=>s.id===id)?.name||id);label.style.color=sourceColor(id);frame.append(label);return label;});
      const reset=el('button','','1:1');reset.setAttribute('aria-label','Enquadrar matriz da CABINE');tools.append(reset);viewport=installMatrixViewport({frame,grid,headers,labels,changed:z=>{reset.textContent=`${Math.round(z*100)}%`;}});reset.onclick=()=>viewport.reset();root.resetMatrixViewport=reset.onclick;
    }
    root.cancelGestures=()=>{viewport?.cancel();cancelPads();};refresh();
    if(mode==='ROTAS') {const labels=el('div','cabine-source-labels');sourceIds.forEach(id=>{const src=meta.sources.find(s=>s.id===id),label=el('span','',src?.name);label.style.color=sourceColor(id);labels.append(label);});root.append(labels);}
  }
  render.tapLfo=(index,time)=>userPerformance.tap(index,time);render.tapLfoText=index=>userPerformance.tapText[index];render.tapDelay=time=>userPerformance.tap(3,time);render.tapDelayText=()=>userPerformance.tapText[3];
  render.runAction=(index,down,value,pressure=false,time=performance.now())=>{
    if(index===8||index===9){setParam(index===8?'dly_freeze':'vap_freeze',down?1:0);return;}
    if(index===10||index===11){setNormalized(index===10?'macro1':'macro2',down?value:0);return;}
    if(!down||pressure)return;
    if(index>=12){userPerformance.tap(index-12,time);return;}
    const config=state.cabine=cabineLayout(state.cabine);
    if(index<3){config.layer='SESSION';config.mode=['ROTAS','SONS','GESTOS'][index];config.page=Math.max(0,Math.min(config.mode==='ROTAS'?1:config.mode==='SONS'?Math.ceil(library().length/56)-1:0,config.page));if(index===1)config.page=Math.floor(Math.max(0,library().findIndex(p=>p.name===state.preset))/56);}
    else if(index===3||index===4){const n=config.page+(index===3?-1:1);if(config.layer==='KEYS'||config.layer==='DRUMS'&&config.melody)config.octave=Math.max(0,Math.min(9,n));else if(config.layer==='SESSION')config.page=Math.max(0,Math.min(config.mode==='ROTAS'?1:config.mode==='SONS'?Math.ceil(library().length/56)-1:0,n));}
    else {const list=library(),current=Math.max(0,list.findIndex(p=>p.name===state.preset));const next=index===7?list.find(p=>p.name==='INIT'):list[(current+(index===6?1:list.length-1))%list.length];if(next)choosePreset(next);return;}
    changed();
  };
  return render;
}
