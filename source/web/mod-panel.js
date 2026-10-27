// SPDX-License-Identifier: GPL-3.0-only
import {mountControllerPanel} from './controller-panel.js';
import {sourceColor} from './controls.js';
import {mountModRows,mountMacroRows,combFormulas,randomCaptions,functionSustain} from './mod-layout.js';
import {envelopeView} from './synth-visuals.js';
import {mountFunctionEditor} from './function-editor.js';
import {mountLfoActions} from './lfo-actions.js';
import {createEnvelopeLink} from './envelope-link.js';
const families=['ENVELOPES','LFO','FUNCTIONS','RANDOM','COMBINATE','MACROS','CONTROLLERS'];

export function createModPanel({el,param,setParam,scopes,metadata,descriptors,field,section,subtabs,rerender,state,send,renderRoutes,learnController,persist,forgetController,learning,feedback,tapLfo=()=>{},tapText=()=>''}){
  const envelopeLink=createEnvelopeLink({param,setParam});
  let family=0,selected=Array(5).fill(0),lfoActions=null;const drafts=Array.from({length:8},()=>({kind:'cc',channel:-1}));
  function controls(root,prefix,family,title){const card=section(root,title,[]);card.dataset.modControls=family;mountModRows({root:card,el,prefix,family,param,descriptor:id=>descriptors().find(d=>d.id===id),field});return card;}
  function render(root){lfoActions=null;const nav=subtabs(families,family,i=>{family=i;rerender();});nav.classList.add('mod-families');nav.style.setProperty('--family-color',sourceColor(['env','lfo','func','rand','comb','mac','ctrl'][family]));root.append(nav);const board=el('div','mod-board');board.dataset.modFamily=family;board.style.setProperty('--family-color',sourceColor(['env','lfo','func','rand','comb','mac','ctrl'][family]));const content=el('div','mod-content');content.append(board);root.append(content);
    if(family<5){const labels=family===0?['ENV 1','ENV 2 / EG1','ENV 3 / EG2']:Array.from({length:3},(_,i)=>`${['','LFO','FUNC','RAND','COMB'][family]} ${i+1}`);board.append(subtabs(labels,selected[family],i=>{selected[family]=i;if(family===0)envelopeLink.select(i);rerender();}));}
    const index=selected[family]||0,k=index+1;
    const workspace=el('div','mod-workspace'),controlHost=el('div','mod-controls'),visualHost=el('div','mod-visual');if(family<5){workspace.append(...(family<2?[controlHost,visualHost]:[visualHost,controlHost]));board.append(workspace);}
    const scope=(id,options)=>{const source=metadata().sources.find(s=>s.id===id);if(source)scopes.mount(visualHost,source,options);};
    if(family===0){const prefix=['env1_','eg1_','eg2_'][index],s=controls(controlHost,prefix,'envelope',['ENV 1','ENV 2 / EG1','ENV 3 / EG2'][index]);const graph=envelopeView({param,prefix,times:metadata().envMs,setParam,interactive:true});graph.dataset.readback='';const link=el('button','envelope-link','LINK DEC / REL');link.setAttribute('aria-label','LINK DEC / REL');link.dataset.readback='';link.sync=()=>link.setAttribute('aria-pressed',envelopeLink.enabled);link.onclick=()=>{envelopeLink.setEnabled(!envelopeLink.enabled);link.sync();};link.sync();s.append(graph);visualHost.append(link);scope(`env${k}`);}
    else if(family===1){controls(controlHost,`lfo${k}_`,'lfo',`LFO ${k}`);lfoActions=mountLfoActions({root:visualHost,el,index,param,setParam,tap:tapLfo,readback:tapText,stateRoot:board});scope(`lfo${k}`);}
    else if(family===2){controls(controlHost,`func${k}_`,'func',`FUNC ${k}`);const editor=mountFunctionEditor({el,points:()=>state.functions[index],sustain:()=>functionSustain(param(`func${k}_mode`),param(`func${k}_sustain`),state.functions[index].length),change:points=>{state.functions[index]=points;send({type:'function',index,points});},});visualHost.append(editor);scope(`func${k}`,{indicator:value=>editor.setLive(value,!!param(`func${k}_polarity`))});}
    else if(family===3){const prefix=`rand${k}_`;controls(controlHost,prefix,'rand',`RAND ${k}`);scope(`rand${k}`,{caption:()=>randomCaptions[param(prefix+'type')]});}
    else if(family===4){controls(controlHost,`comb${k}_`,'comb',`COMB ${k}`);scope(`comb${k}`,{caption:()=> '= '+combFormulas[param(`comb${k}_type`)]});}
    else if(family===5){const card=section(board,'MACROS',[]);mountMacroRows({root:card,el,param,descriptor:id=>descriptors().find(d=>d.id===id),field});}
    else{
      mountControllerPanel({root:board,el,state,metadata,field,descriptor:id=>descriptors().find(d=>d.id===id),persist,learn:learnController,forget:forgetController,learning,feedback,drafts,rerender});
    }
    const routes=el('details','mod-route-list');const summary=el('summary');routes.append(summary);routes.addEventListener('routecount',()=>summary.textContent=`MODULATION ROUTES (${state.routes.length}/64)`);renderRoutes(routes);routes.open=matchMedia('(min-width:1100px)').matches;content.append(routes);
  }
  return {render,syncEnvelope:envelopeLink.refresh,refresh:()=>lfoActions?.sync()};
}
