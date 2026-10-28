// SPDX-License-Identifier: GPL-3.0-only
import {mountOperatorOverview} from './operator-overview.js';
import {controlChoices} from './controls.js';
import {algorithmState,algorithmSvg,envelopeView} from './synth-visuals.js';
import {operatorRatioText,voiceFilterState} from './synth-parameters.js';
import {operatorSections,mountOperatorRows,mountOperatorMode} from './operator-layout.js';
import {filterRows,mountFilterRows} from './filter-layout.js';
import {filterResponseView} from './filter-response.js';
const modes=['FM','RING','FILTER','FILTER FM','FOLD','EFFECT','BYPASS','MUTE','MODAL','CHAOS','STOCHASTIC','MACRO','SAMPLE','UNISON'];
export function createSynthPanel({el,param,descriptor,descriptors,metadata,algorithms,field,section,subtabs,setParam,rerender,renderModulation,filterRate,samplePanel}){
  let op=0,page=0,modalPage=0,transient,expanded=!matchMedia('(max-height: 500px)').matches;
  const get=ids=>ids.map(id=>descriptor(id)).filter(Boolean);
  const close=()=>{transient?.close();transient?.remove();transient=null;};
  function dialog(title){close();const d=el('dialog','instrument-dialog'),head=el('div','instrument-dialog-head'),exit=el('button','','Fechar');head.append(el('h2','',title),exit);d.append(head);document.body.append(d);exit.onclick=close;d.addEventListener('close',()=>{if(transient===d)transient=null;d.remove();});transient=d;return d;}
  function links(a=param('algo')){return algorithmState(algorithms(),a,param('alg_fb'),param);}
  function picker(){const d=dialog('ALGORITHM'),grid=el('div','algorithm-picker');for(let i=0;i<=40;i++){const b=el('button',i===param('algo')?'active':'');b.setAttribute('aria-label',i===40?'Algoritmo USER':`Algoritmo ${i+1}`);b.setAttribute('aria-pressed',i===param('algo'));b.dataset.algorithm=i;b.append(el('span','',i===40?'USER':String(i+1)),algorithmSvg(links(i)));b.onclick=()=>{setParam('algo',i,true);close();rerender();};grid.append(b);}d.append(grid);d.showModal();}
  function matrix(){const d=dialog('USER / MATRIX'),grid=el('div','user-matrix');grid.append(el('span','','FROM / TO'));for(let t=1;t<=6;t++)grid.append(el('span','',`OP${t}`));for(let f=1;f<=6;f++){grid.append(el('span','',`OP${f}`));for(let t=1;t<=6;t++){const n=field({...descriptor(`mx_${f}to${t}`),name:`${f} → ${t}`});n.querySelector('.dial').ondblclick=()=>setParam(`mx_${f}to${t}`,param(`mx_${f}to${t}`)?0:100);grid.append(n);}}grid.append(el('span','','OUT'));for(let t=0;t<6;t++){const b=el('button','matrix-out',`OP${t+1}`);b.setAttribute('aria-label',`Saída direta OP${t+1}`);b.onclick=()=>{setParam('direct_out',param('direct_out')^(1<<t));};b.dataset.readback='';b.sync=()=>b.setAttribute('aria-pressed',!!(param('direct_out')>>t&1));b.sync();grid.append(b);}d.append(grid);d.showModal();}
  function operator(root){
    const board=el('div','operator-board native-operator-board'),prefix=`op${op+1}_`;board.dataset.operatorMode=param(prefix+'mode');root.append(board);
    const add=(key,title)=>{const card=section(board,`OP${op+1}  ${title}`,[]);card.dataset.synthSection=key;return card;};
    const osc=add('oscillator','OSCILLATOR');mountOperatorRows({root:osc,el,prefix,rows:operatorSections.oscillator,choices:new Set(['freq','coarse']),descriptor,field});
    const exp=add('expression','LEVEL / VELOCITY / KEY TRACK'),coarse=el('button','velocity-coarse','VEL PITCH > COARSE');coarse.setAttribute('role','switch');coarse.onclick=()=>setParam('vel_coarse',param('vel_coarse')^(1<<op));coarse.dataset.readback='';coarse.sync=()=>coarse.setAttribute('aria-checked',!!(param('vel_coarse')>>op&1));coarse.sync();mountOperatorRows({root:exp,el,prefix,rows:operatorSections.expression,descriptor,field,special:coarse});
    const env=add('envelope',param(prefix+'mode')===8?'ENVELOPE > EXCITATION':'ENVELOPE');mountOperatorRows({root:env,el,prefix,rows:operatorSections.envelope,descriptor,field});
    const graph=envelopeView({param,prefix,times:metadata().envMs,setParam,interactive:true});graph.dataset.readback='';const syncEnvelope=graph.sync;graph.sync=()=>{syncEnvelope();graph.classList.toggle('inactive-param',param(prefix+'level')===0||param(prefix+'mode')===7);};graph.sync();env.append(graph);
    const mode=param(prefix+'mode'),modeRoot=add('mode',`MODE: ${modes[mode]}`);mountOperatorMode({root:modeRoot,el,prefix,mode,fx:param(prefix+'a'),models:metadata().models,modalPage,descriptor,field,subtabs,showWave:!expanded,onModalPage:i=>{modalPage=i;rerender();}});if(mode===12)samplePanel?.render(modeRoot,prefix);
  }
  function filters(root){
    const board=el('div','operator-board filter-board native-filter-board');root.append(board);
    const state=voiceFilterState(param('filter_type'),!!param('filter'));
    const add=(parent,title,rows)=>{const card=section(parent,title,[]);mountFilterRows({root:card,el,rows,descriptor,field,state});return card;};
    const main=add(board,'VOICE FILTER',filterRows.common);main.dataset.filterSection='main';
    if(state.special.length){const extra=add(main,state.casc?'SK CASC (HP > LP)':'CMOS',state.casc?filterRows.cascade:filterRows.cmos);extra.classList.add('filter-type-controls');extra.dataset.filterSection=state.casc?'cascade':'cmos';}
    main.append(filterResponseView({param,setParam,rate:filterRate}));
    const env=add(board,'EG2 (FILTER ENVELOPE)',filterRows.envelope);env.dataset.filterSection='envelope';
    const graph=envelopeView({param,prefix:'eg2_',times:metadata().envMs,setParam,interactive:true});graph.dataset.readback='';graph.classList.toggle('inactive-param',state.dim);env.append(graph);
    const output=el('div','filter-output-column');board.append(output);add(output,'OUTPUT',filterRows.output).dataset.filterSection='output';add(output,'LPG',filterRows.lpg).dataset.filterSection='lpg';
  }
  function render(root){close();root.classList.add('synth-view');const overview=el('div','synth-overview'),algo=el('section','algorithm-overview'),head=el('button','algorithm-title',`ALGORITHM / ${param('algo')===40?'USER':param('algo')+1}`);head.onclick=picker;head.setAttribute('aria-label','Escolher algoritmo pelos desenhos');const art=el('button','algorithm-diagram');art.onclick=()=>param('algo')===40?matrix():picker();art.setAttribute('aria-label',param('algo')===40?'Editar matriz USER':'Escolher algoritmo pelos desenhos');art.dataset.readback='';art.sync=()=>art.replaceChildren(algorithmSvg(links()));art.sync();algo.append(head,art);
    const controls=el('div','algorithm-controls');controls.append(field(descriptor('phase')),field({...descriptor('alg_fb'),name:'ALG FB',instance:'overview',dim:![3,5,33].includes(param('algo'))}));algo.append(controls);overview.append(algo);
    for(let k=0;k<6;k++){
      const prefix=`op${k+1}_`,strip=el('section',`operator-strip${op===k?' selected':''}`),select=el('button','operator-select');select.dataset.operator=k;select.setAttribute('aria-label',`Selecionar operador ${k+1}`);select.setAttribute('aria-pressed',op===k);const status=el('span','operator-role'),summary=el('span','operator-summary'),ratio=el('output','operator-ratio');select.append(el('strong','',`OP${k+1}`),status,summary);select.onclick=()=>{op=k;page=0;rerender();};strip.append(select);
      summary.dataset.readback='';summary.sync=()=>{const w=descriptor(prefix+'wave');summary.textContent=`${modes[param(prefix+'mode')]} / ${controlChoices(w)[param(prefix+'wave')]}`;};summary.sync();if(expanded)mountOperatorOverview({el,strip,prefix,mode:param(prefix+'mode'),descriptor,field});
      status.dataset.readback='';status.sync=()=>status.textContent=links().carriers>>k&1?'OUT':'MOD';status.sync();
      ratio.dataset.readback='';ratio.sync=()=>{ratio.textContent=operatorRatioText(param,prefix);ratio.classList.toggle('inactive-param',param(prefix+'level')===0||param(prefix+'mode')===7);};ratio.sync();
      const level=field({...descriptor(prefix+'level'),name:'LEVEL',instance:'overview'});strip.append(ratio,level);const env=envelopeView({param,prefix,times:metadata().envMs});env.dataset.readback='';const sync=env.sync;env.sync=()=>{sync();env.classList.toggle('inactive-param',param(prefix+'level')===0||param(prefix+'mode')===7);};env.sync();strip.append(env);overview.append(strip);
    }
    overview.classList.toggle('overview-compact',!expanded);
    root.append(overview);const tabs=subtabs(['OPERATOR','FILTER / EG2 / OUTPUT','MOD'],page,i=>{page=i;rerender();});tabs.classList.add('synth-pages');const expand=el('button','overview-toggle',expanded?'RECOLHER':'VISÃO GERAL');expand.setAttribute('aria-label',expanded?'Recolher visão geral dos operadores':'Expandir visão geral dos operadores');expand.setAttribute('aria-expanded',expanded);expand.onclick=()=>{expanded=!expanded;rerender();};tabs.append(expand);root.append(tabs);
    if(page===0)operator(root);else if(page===1)filters(root);else renderModulation(root);
  }
  return {render,close,selectPage(value){page=value;}};
}
