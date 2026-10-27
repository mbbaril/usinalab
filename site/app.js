// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Mario Baril
import {createPresetBrowser,presetCategories} from './preset-browser.js';
import {mountPresetExport} from './preset-export-ui.js';
import {factoryParameters} from './factory-preset.js';
import {mountRouteList} from './route-list.js';
import {installTypingKeyboard,releasePointerChoiceFocus} from './typing-keyboard.js';
import { MidiConnection } from './midi.js';
import { parseVstSound, applyVstSound } from './preset-import.js';
import { controlChoices, choiceDisabled, displayValue, sourceColor, configureControls, parameterCaption } from './controls.js';
import { installPerformance } from './performance.js';
import { createCabine } from './cabine.js';
import { createMpeController } from './mpe-controller.js';
import { mountModulationTray } from './modulation-tray.js';
import { sourceBank,openSourcePicker,closeSourcePicker } from './modulation-sources.js';
import { installInstrumentShell } from './instrument-shell.js';
import {createFxPanel} from './fx-panel.js';
import {validDurations} from './note-duration.js';
import { renderCredits } from './credits.js';
import { MidiPickup } from './midi-pickup.js';
import {installVerticalGesture} from './vertical-gesture.js';
import { mountMidiLearn,closeMidiMenu } from './midi-learn-ui.js';
import { cabineLayout } from './cabine-keys.js';
import { createCabineActions,actionBindings } from './cabine-actions.js';
import { exportMappings,importMappings } from './mapping-files.js';
import { createMappingPanel } from './mapping-panel.js';
import { createSynthPanel } from './synth-panel.js';
import { createModPanel } from './mod-panel.js';
import { createLocalKeyboard } from './local-keyboard.js';
import {createModulationFeedback} from './modulation-feedback.js';
import {createSourceScopes} from './source-scope.js';
import {createSequencePanel} from './sequence-panel.js';
import {createSequenceGenerator} from './sequence-generator.js';
const $ = id => document.getElementById(id);
if (window.Capacitor?.isNativePlatform()) document.documentElement.classList.add('native-app');
const el = (tag, cls, text) => { const n = document.createElement(tag); if (cls) n.className = cls; if (text !== undefined) n.textContent = text; return n; };
const shell=installInstrumentShell({el}),pickup=new MidiPickup();
const categories = Object.fromEntries(presetCategories);
const modes = ['FM', 'RING', 'FILTER', 'FILTER FM', 'FOLD', 'EFFECT', 'BYPASS', 'MUTE', 'MODAL', 'CHAOS', 'STOCHASTIC', 'MACRO'];
const effects = ['NONE', 'TEXTURE', 'ROOM', 'DISTORT', 'IR · pendente', 'VAPOR', 'DELAY'];
const fxSlots = ['fx_a', 'fx_b', 'aux', 'loop'].flatMap(bus => ['', '2', '3'].map(slot => `${bus}${slot}_type`));
const state = { params: {}, routes: [], sequence: Array.from({ length: 9 }, (_, lane) => Array.from({ length: 64 }, () => ({ on: false, inherit: true, note: lane >= 5 ? [36, 41, 48, 55][lane - 5] : 60, velocity: lane > 0 && lane < 5 ? 64 : 100, probability: 1, ratchet: 1, gate: .5 }))), lanes: Array.from({ length: 9 }, () => ({ length: 16, direction: 0, mul: 1, div: 1, mute: false })), resolution: 4, swing: 0, preset: 'CORDA FM' };
let context, node, analyser, meta, algorithmData, descriptors = [], seqCursors = Array(9).fill(-1), byId = new Map(), byName = new Map(), library = [], view = 'operators', lane = 0, midi, inputStream, inputNode, pendingParams = new Map(), flushQueued = false, audioReady = false;
let selectedPreset, selectedStep = 0, learnParameter = null, learnControl = -1, selectedSource = null, directLearn = false, layoutPending = false, sourcesExpanded = false;
const keyboardVisibility={perform:true,operators:true,modulation:true,effects:true,sequence:true};
$('keybed-toggle').onclick=()=>{if(keyboardVisibility[view])localKeyboard.stop();keyboardVisibility[view]=!keyboardVisibility[view];renderView();};
state.ccMaps = [];
state.controllerBindings = Array(8).fill(null);
const defaultFunctions = [[[0, 1, -.6], [1, 0, 0]], [[0, 0, 0], [.5, 1, 0], [1, 0, 0]], [[0, 0, .3], [1, 1, 0]]];
const cloneFunctions = () => defaultFunctions.map(f => f.map(p => [...p]));
state.functions = cloneFunctions();
const message = (text, error = false) => { $('message').textContent = text; $('message').classList.toggle('error', error); };
const send = data => { if (audioReady) node.port.postMessage(data); };
const sourceScopes=createSourceScopes({el,send,ready:()=>audioReady&&context?.state==='running'});
const param = id => state.params[id] ?? byId.get(id)?.default ?? 0;
const presetBrowser=createPresetBrowser({el,root:document.querySelector('.library'),current:()=>selectedPreset,choose:(p,close)=>choosePreset(p,true,close),close:()=>shell.closeLibrary(),message});
mountPresetExport({el,tools:document.querySelector('.preset-browser-tools'),snapshot:()=>structuredClone(state),metadata:()=>meta,save:saveDocument,closeLibrary:()=>shell.closeLibrary(),message});
$('presets-toggle').addEventListener('click',()=>{if(document.documentElement.classList.contains('library-open'))presetBrowser.opened();});
const modulationFeedback=createModulationFeedback({metadata:()=>meta,state,param,send,ready:()=>audioReady&&context?.state==='running'&&!document.hidden});
const performanceControls = installPerformance({ note: (...args) => { if (audioReady) note(...args); }, getRange: () => param('bend_range') || 2, setRange: v => setParam('bend_range', v),isMpe:()=>param('mpe_mode')>.5 });
const localKeyboard=createLocalKeyboard({el,note:(...args)=>{const packet=sequencePanel.midi(...args,view==='sequence','keybed');if(packet)note(...packet);},param,setParam,ready:()=>audioReady});
const sequenceGenerator=createSequenceGenerator({state,send,message,changed:()=>{if(view==='sequence')sequencePanel.refresh();}});
const sequencePanel=createSequencePanel({el,state,param,setParam,send,subtabs,generator:sequenceGenerator,metadata:()=>meta,cursors:()=>seqCursors});
const fxPanel=createFxPanel({el,param,setParam,descriptors:()=>descriptors,metadata:()=>meta,field,section:controlSection,subtabs,slots:fxSlots,effects,rerender:renderView,tapDelay:time=>renderCabine.tapDelay(time),tapText:()=>renderCabine.tapDelayText()});
const synthPanel=createSynthPanel({el,param,descriptor:id=>byId.get(id),descriptors:()=>descriptors,metadata:()=>meta,algorithms:()=>algorithmData,field,section:controlSection,subtabs,setParam,rerender:renderView,renderModulation,filterRate:()=>({rate:(context?.sampleRate||48000)*(+$('quality').value),firmware:+$('quality').value===1})});
const modPanel=createModPanel({tapLfo:(i,time)=>renderCabine.tapLfo(i,time),tapText:i=>renderCabine.tapLfoText(i),el,param,setParam,scopes:sourceScopes,metadata:()=>meta,descriptors:()=>descriptors,field,section:controlSection,subtabs,rerender:renderView,state,send,renderRoutes:renderRouteList,feedback:modulationFeedback,learning:()=>learnControl,persist:persistMappings,learnController:i=>{cabineActionMidi.stopLearn();learnParameter=null;directLearn=false;selectedSource=null;learnControl=learnControl===i?-1:i;renderAddressBar();refreshAddresses();if(learnControl>=0){ensureMidi();message(`CTRL ${i+1}: mova um CC ou toque uma nota.`);}else message('MIDI Learn encerrado.');},forgetController:i=>{if(learnControl===i)learnControl=-1;state.controllerBindings[i]=null;send({type:'control',index:i,value:0});persistMappings();message(`CTRL ${i+1}: Forget.`);}});
const renderCabine = createCabine({ el, state, metadata: () => meta, param, descriptors: () => descriptors, setParam, send, note, library: () => library, choosePreset, message, cursors:()=>seqCursors,changed:()=>{if(view==='cabine')renderView();} });
const cabineActionMidi=createCabineActions({state,run:(...args)=>{renderCabine.runAction(...args);modPanel.refresh();if(view==='effects')$('view').refreshFx?.();if(view==='cabine')$('view').refreshCabine?.();},changed:keep=>{if(keep)persistMappings();mappingPanel.refresh();}});
const mappingPanel=createMappingPanel({el,state,metadata:()=>meta,actions:cabineActionMidi,prepareLearn:()=>{learnParameter=null;learnControl=-1;directLearn=false;selectedSource=null;renderAddressBar();refreshAddresses();ensureMidi();},save:saveDocument,replace:replaceMappings,message});
const mpeController = createMpeController({ el, status: message });
function persistMappings(){try{localStorage.setItem('usina.mappings.v1',exportMappings(state));}catch{message('Não foi possível guardar o mapa automaticamente. Use Exportar mapa.',true);}}
function replaceMappings(next){releaseAll();cabineActionMidi.stopLearn();learnParameter=null;learnControl=-1;directLearn=false;const {ignored,...maps}=next;Object.assign(state,maps);pickup.reset();persistMappings();refreshAddresses();if(view==='connections')renderView();}
function ensureMidi(){if(!midi)midi=new MidiConnection(handleMidi,text=>message(text),releaseAll);if(!midi.connected&&!midi.access)midi.connect().catch(e=>message(e.message,true));}
async function saveDocument(text,name){if(window.Capacitor?.isNativePlatform()){const {registerPlugin}=await import('./capacitor-core.js');await registerPlugin('UsinaStorage').save({text,name});return;}const a=el('a');a.href=URL.createObjectURL(new Blob([text],{type:'application/xml'}));a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);}
function setParam(id, value, immediate = false, origin = 'ui') {
  const d = byId.get(id); if (!d || !Number.isFinite(+value)) return;
  value = Math.min(d.max, Math.max(d.min, +value));
  value = Math.min(d.max, Math.max(d.min, d.min + Math.round((value - d.min) / d.step) * d.step));
  state.params[id] = value; pendingParams.set(d.index, value);if(id==='mpe_mode')sequencePanel.syncMidiMode();
  if(id==='mpe_mode'||id==='mpe_bend_range')immediate=true;
  if(origin!=='midi')pickup.reset(id);
  if (fxSlots.includes(id)) {
    if (value) for (const other of fxSlots) if (other !== id && param(other) === value) { state.params[other] = 0; pendingParams.set(byId.get(other).index, 0); }
    layoutPending = true;
  }
  const operatorType=id.match(/^(op\d)_(mode|a)$/);
  if (operatorType&&(operatorType[2]==='mode'||[2,3,5,8,9,10,11].includes(param(`${operatorType[1]}_mode`))) || /^rand\d_type$/.test(id) || ['algo','vap_mode','dist_mode','filter_type','filter'].includes(id)) layoutPending = true;
  if (immediate) flush(); else if (!flushQueued) { flushQueued = true; requestAnimationFrame(flush); }
}
function flush() { flushQueued = false; modPanel.syncEnvelope(); if (pendingParams.size) { send({ type: 'params', values: [...pendingParams] }); pendingParams.clear(); if (layoutPending) { layoutPending = false; renderView(); } else document.querySelectorAll('[data-param],[data-readback]').forEach(n => n.sync?.());localKeyboard.sync();if(view==='cabine')$('view').refreshCabine?.(); } }
function sendState() {
  modPanel.syncEnvelope();
  send({ type: 'patch', values: descriptors.map(d => [d.index, param(d.id)]) });
  state.routes.forEach((r, index) => send({ type: 'route', index, ...r }));
  state.functions.forEach((points, index) => send({ type: 'function', index, points }));
  send({ type: 'globals', resolution: state.resolution, swing: state.swing });
  send({ type: 'sequence', sequence: state.sequence, lanes: state.lanes });
}
async function startAudio() {
  if (!window.AudioContext || !window.AudioWorkletNode || !window.WebAssembly) throw Error('Este navegador não oferece o áudio necessário. Atualize o navegador ou use o app Usina.');
  $('start').disabled = true; $('start').textContent = 'Preparando…';
  try {
    if (!context) { context = new AudioContext({ latencyHint: 'interactive' }); context.onstatechange = () => { sourceScopes.suspend(context.state!=='running'||document.hidden);modulationFeedback.suspend(context.state!=='running'||document.hidden);if (context.state !== 'running') releaseAll(); $('audio-status').textContent = context.state === 'running' ? `${context.sampleRate / 1000} kHz` : 'ÁUDIO PAUSADO'; }; }
    await context.resume();
    const response = await fetch('./usina.wasm'); if (!response.ok) throw Error('Motor de áudio não encontrado');
    const module = await WebAssembly.compile(await response.arrayBuffer());
    if (node) {
      releaseAll(); const old = node; audioReady = false;
      await new Promise(resolve => { const timeout = setTimeout(resolve, 2000); old.port.onmessage = ({ data }) => { if (data.type === 'disposed') { clearTimeout(timeout); resolve(); } }; old.port.postMessage({ type: 'dispose' }); });
      old.disconnect(); old.port.close();
    }
    await context.audioWorklet.addModule('./worklet.js');
    const seed = crypto.getRandomValues(new Uint32Array(1))[0];
    const nextNode = new AudioWorkletNode(context, 'usina', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2], processorOptions: { module, oversampling: +$('quality').value, seed } });
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(Error('O motor demorou para iniciar. Tente uma qualidade menor.')), 45000);
      nextNode.onprocessorerror = () => { audioReady = false; message('O motor de áudio parou. Ative o áudio novamente.', true); reject(Error('Falha no AudioWorklet')); };
      nextNode.port.onmessage = ({ data }) => {
        if (data.type === 'ready') { clearTimeout(timer); meta = data.metadata; resolve(); }
        else if (data.type === 'error') { clearTimeout(timer); message(data.message, true); reject(Error(data.message)); }
        else if(data.type==='modSources'){modulationFeedback.receive(data);}
        else if(data.type==='source'){sourceScopes.receive(data);}
        else if (data.type === 'meter') {
          if(data.cursors)seqCursors=data.cursors;
          if(view==='cabine')$('view').refreshCabine?.();if(view==='sequence')$('view').refreshSequence?.();
          $('voices').textContent = `${String(data.voices).padStart(2, '0')} VOZES`;
          document.querySelectorAll('.step').forEach((s, i) => s.classList.toggle('cursor', i === data.cursor && lane === 0));
        }
      };
    });
    node = nextNode; audioReady = true;modulationFeedback.suspend(false);
    configureControls(meta);
    if (!descriptors.length) {
      descriptors = meta.parameters.map((d, index) => ({ ...d, index }));
      byId = new Map(descriptors.map(d => [d.id, d])); byName = new Map(descriptors.map(d => [d.name, d]));
      for (const d of descriptors) state.params[d.id] ??= d.default;
      choosePreset(selectedPreset || library.find(p => p.name === state.preset) || library[0], false);
    } else sendState();
    if (!analyser) { analyser = context.createAnalyser(); analyser.fftSize = 2048; analyser.connect(context.destination); }
    node.connect(analyser); if (inputNode) inputNode.connect(node);
    $('audio-status').textContent = `${context.sampleRate / 1000} kHz / ${$('quality').value}×`;
    document.querySelectorAll('.filter-response').forEach(n=>n.sync());
    $('start').textContent = 'Áudio ativo ●'; $('start').disabled = false; $('audition').disabled = false;
    message('Pronto. Toque no teclado ou use seu controlador.');sequencePanel.syncMidiMode(true);
    renderView();
  } catch (error) { $('start').disabled = false; $('start').textContent = 'Ativar áudio ↗'; throw error; }
}
$('start').onclick = async () => {
  try {
    if (audioReady) {
      if (context.state === 'running') { releaseAll(); setParam('seq_play', 0, true); await context.suspend(); $('start').textContent = 'Retomar áudio ↗'; message('Áudio pausado.'); }
      else { await context.resume(); $('start').textContent = 'Áudio ativo ●'; message('Áudio retomado.'); }
      return;
    }
    await startAudio();
  } catch (e) { message(e.message, true); }
};
$('quality').onchange = () => { document.querySelectorAll('.filter-response').forEach(n=>n.sync());if (context) startAudio().catch(e => message(e.message, true)); };
// The default is fidelity-first on every device. The quality selector is explicit: no silent downgrade on mobile.
function choosePreset(p, notify = true, close = true) {
  pickup.reset();if(close)shell.closeLibrary();
  if (!p) return; selectedPreset = p; state.preset = p.name;
  shell.setPreset(p.name);
  $('patch-name').textContent = p.name; $('patch-category').textContent = `${categories[p.cat] || p.cat} / USINA`;
  $('patch-desc').textContent = p.desc?.pt || 'Um seno. O ponto de partida para construir o seu som.';
  if (descriptors.length) {
    state.params = factoryParameters(state.params,p,meta);
    state.routes = []; state.functions = cloneFunctions(); pendingParams.clear(); sendState(); renderView();
  }
  renderLibrary(); if (notify) message(`${p.name} carregado. Pressione OUVIR ou toque uma nota.`);
}
function renderLibrary(){presetBrowser.refresh();}
function renderCategories(){presetBrowser.refresh();}

function format(d, v) { return displayValue(d, v); }
function knob(id, title, descriptor = null) {
  const d = descriptor || byId.get(id); if (!d) return el('div');
  const n = el('div', 'knob'), label = el('label', '', title || d.name), dial = el('div', 'dial'), input = el('input');
  const unique = `knob-${id}${descriptor?.instance?'-'+descriptor.instance:''}`; input.id = unique; label.htmlFor = unique; input.type = 'number'; input.min = d.min; input.max = d.max; input.step = d.step;
  dial.tabIndex = 0; dial.setAttribute('role', 'slider'); dial.setAttribute('aria-label', title || d.name); dial.setAttribute('aria-valuemin', d.min); dial.setAttribute('aria-valuemax', d.max);
  const skew = /^op\d_fixed_hz$/.test(id) ? .25 : d.skew || 1;
  const readout = descriptor ? el('output', 'knob-value') : input;
  const update = v => { v = Math.min(d.max, Math.max(d.min, v)); const ratio = Math.pow((v - d.min) / (d.max - d.min), skew), origin = d.min < 0 && d.max > 0 ? Math.pow(-d.min / (d.max - d.min), skew) : 0; dial.style.setProperty('--arc-start', `${Math.min(origin, ratio) * 270}deg`); dial.style.setProperty('--arc-end', `${Math.max(origin, ratio) * 270}deg`); dial.style.setProperty('--turn', `${ratio * 270}deg`); dial.style.setProperty('--angle', `${ratio * 270 - 135}deg`); input.value = Number.isInteger(v) ? String(v) : String(+v.toFixed(3)); if (descriptor) readout.textContent = format(d, v); dial.setAttribute('aria-valuenow', v); dial.setAttribute('aria-valuetext', format(d, v)); };
  update(param(id));
  n.cancelParameterDrag=installVerticalGesture({target:dial,get:()=>Math.pow((param(id)-d.min)/(d.max-d.min),skew),set:ratio=>{setParam(id,d.min+(d.max-d.min)*Math.pow(Math.min(1,Math.max(0,ratio)),1/skew));update(param(id));}});
  dial.onkeydown = e => { if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(e.key)) { e.preventDefault(); const delta = e.key === 'ArrowUp' || e.key === 'ArrowRight' ? 1 : -1; const v = e.key === 'Home' ? d.min : e.key === 'End' ? d.max : param(id) + delta * Math.max(d.step, (d.max - d.min) / 100); setParam(id, v); update(param(id)); } };
  dial.ondblclick = () => { setParam(id, d.default); update(param(id)); };
  input.onchange = () => { setParam(id, +input.value); update(param(id)); };
  n.dataset.param = id; n.sync = () => update(param(id));
  n.append(label, dial, readout); attachAddressing(n, d); return n;
}
function field(d) {
  d = { ...d, name: d.uiCaption??parameterCaption(d) };
  const categorical = controlChoices(d) || /^op\d_mode$/.test(d.id) || fxSlots.includes(d.id);
  if (!categorical) {
    const root = knob(d.id, d.name, d), syncKnob = root.sync;
    root.sync = () => { syncKnob(); const match = d.id.match(/^(op\d)_(coarse|fine|detune|trans|fixed_hz)$/); const inactive = match && (match[2] === 'fixed_hz' ? !param(`${match[1]}_freq`) : !!param(`${match[1]}_freq`)); root.classList.toggle('inactive-param', !!inactive || !!(typeof d.dim==='function'?d.dim():d.dim)); };
    root.sync(); return root;
  }
  const root = el('label', 'field'), head = el('span', 'field-head'), output = el('output', '', format(d, param(d.id)));
  head.append(el('span', '', d.name), output); root.append(head);
  let choices = controlChoices(d);
  if (/^op\d_mode$/.test(d.id)) choices = modes;
  if (d.id.endsWith('_type') && ['FX A', 'FX B', 'AUX', 'LOOP'].includes(d.group)) choices = effects;
  const toggle = choices?.[0] === 'Desligado' && choices?.[1] === 'Ligado';
  if (toggle) { const button = el('button', 'param-toggle'); button.type = 'button'; button.setAttribute('role', 'switch'); button.setAttribute('aria-label', d.name); button.onclick = () => setParam(d.id, param(d.id) ? 0 : 1); root.append(button); }
  else if (choices) { const select = el('select'); select.setAttribute('aria-label', d.name); choices.forEach((c, i) => { const o = el('option', '', c); o.value = i+d.min; if (i === 4 && choices === effects || choiceDisabled(d,i+d.min)) o.disabled = true; select.append(o); }); select.value = d.min+(param(d.id)-d.min)%choices.length; releasePointerChoiceFocus(select); select.onchange = () => { setParam(d.id, +select.value); output.textContent = select.options[select.selectedIndex].text; }; root.append(select); }
  else {
    const input = el('input'); input.type = 'range'; input.min = 0; input.max = 1; input.step = 'any';
    const skew = /^op\d_fixed_hz$/.test(d.id) ? .25 : d.skew || 1;
    input.value = Math.pow((param(d.id) - d.min) / (d.max - d.min), skew);
    input.oninput = () => { setParam(d.id, d.min + (d.max - d.min) * Math.pow(+input.value, 1 / skew)); output.textContent = format(d, param(d.id)); };
    input.dataset.skew = skew; root.append(input);
  }
  root.dataset.param = d.id; root.sync = () => {
    const input = root.querySelector('input, select, button');
    if (toggle) { input.textContent = (typeof d.toggleLabel==='function'?d.toggleLabel():d.toggleLabel)??(d.compactToggle?(param(d.id)?'ON':'OFF'):(param(d.id) ? 'Ligado ●' : 'Desligado ○')); input.setAttribute('aria-checked', !!param(d.id)); }
    else input.value = input.dataset.skew ? Math.pow((param(d.id) - d.min) / (d.max - d.min), +input.dataset.skew) : choices ? d.min+(param(d.id)-d.min)%choices.length : param(d.id);
    output.textContent = choices ? choices[(param(d.id)-d.min) % choices.length] : format(d, param(d.id));
    const match = d.id.match(/^(op\d)_(coarse|fine|detune|trans|fixed_hz)$/);
    const inactive = match && (match[2] === 'fixed_hz' ? !param(`${match[1]}_freq`) : !!param(`${match[1]}_freq`));
    root.classList.toggle('inactive-param', !!inactive||!!(typeof d.dim==='function'?d.dim():d.dim));
  }; root.sync();
  attachAddressing(root, d); return root;
}
function armLearn(id) {
  cabineActionMidi.stopLearn();
  learnParameter = id; learnControl = -1; directLearn = false; selectedSource = null;
  renderAddressBar(); refreshAddresses();
  message(`Mova um CC para controlar ${byId.get(id).name}. Use Cancelar para sair.`);
  if(!midi)midi=new MidiConnection(handleMidi,text=>message(text),releaseAll);
  if(!midi.connected&&!midi.access)midi.connect().then(()=>{if(learnParameter===id)message(`Mova um CC para controlar ${byId.get(id).name}. Use Cancelar para sair.`);}).catch(error=>message(error.message,true));
}
function forgetMidi(id){state.ccMaps=state.ccMaps.filter(m=>m.param!==id);persistMappings();pickup.reset(id);if(learnParameter===id)learnParameter=null;renderAddressBar();refreshAddresses();message(`Forget: ${parameterCaption(byId.get(id))} sem mapeamento MIDI.`);}
function destinationFor(d) { return meta?.destinations.find(t => t.id === d.id); }
function refreshAddresses() {
  document.querySelectorAll('.controller-slot').forEach(n=>n.sync?.());
  document.querySelectorAll('.mod-route-list').forEach(n=>n.refreshRoutes?.());
  document.querySelectorAll('[data-param]').forEach(n => {
    const d = byId.get(n.dataset.param), dst = d && destinationFor(d);
    n.classList.toggle('addressable', directLearn || selectedSource !== null && !!dst);
    n.classList.toggle('has-modulation', !!dst && state.routes.some(r => r.destination === dst.index));
    n.classList.toggle('has-midi', state.ccMaps.some(m => m.param === n.dataset.param));
    n.classList.toggle('learning', learnParameter === n.dataset.param);
    const route = state.routes.find(r => r.destination === dst?.index); n.style.setProperty('--mod-color', sourceColor(meta?.sources.find(s => s.index === route?.source)?.id));
    n.modSync?.();n.modFeedbackSync?.();
    n.midiSync?.();
  });
}
function attachAddressing(root, d) {
  root.midiSync=mountMidiLearn({root,id:d.id,title:d.name,el,maps:()=>state.ccMaps,waiting:m=>pickup.waiting(m),learn:armLearn,forget:forgetMidi});
  const destination=destinationFor(d);
  root.classList.toggle('has-modulation', state.routes.some(r => r.destination === destinationFor(d)?.index));
  root.classList.toggle('has-midi', state.ccMaps.some(m => m.param === d.id));
  root.classList.toggle('addressable', directLearn || selectedSource !== null && !!destinationFor(d));
  const route = state.routes.find(r => r.destination === destinationFor(d)?.index); root.style.setProperty('--mod-color', sourceColor(meta?.sources.find(s => s.index === route?.source)?.id));
  if(destination){
    const tray=mountModulationTray({root,destination:destination.index,title:d.name,metadata:()=>meta,state,send,el,changed:refreshAddresses});root.modSync=()=>{tray.refresh();root.modFeedbackSync?.();};root.modTray=tray;modulationFeedback.mount(root,d,destination.index);
    const originalSync=root.sync;root.sync=()=>{originalSync?.();tray.refresh();root.modFeedbackSync?.();};
    const plus=el('button','mod-add','+');plus.type='button';plus.setAttribute('aria-label',`Adicionar modulação a ${d.name}`);
    plus.onclick=e=>{e.preventDefault();if(selectedSource!==null){editDirectRoute(d);return;}openSourcePicker({anchor:plus,el,metadata:()=>meta,pick:source=>assignRoute(d,source)});};
    const mainDial=root.querySelector(':scope > .dial');if(mainDial){const zone=el('div','dial-zone');mainDial.replaceWith(zone);zone.append(mainDial,plus);}else root.append(plus);
  }
  root.addEventListener('pointerdown', e => {
    if(e.target.closest('.mod-tray,.mod-add,.source-bank,.midi-actions,.midi-badge'))return;
    if (!directLearn && (selectedSource === null || !destinationFor(d))) return;
    e.preventDefault(); e.stopImmediatePropagation();
    if (directLearn) armLearn(d.id);
    else if (destinationFor(d)) editDirectRoute(d);
  }, true);
}
function editDirectRoute(d) {
  if(selectedSource!==null)assignRoute(d,selectedSource);
}
function assignRoute(d,source){const destination=destinationFor(d);if(!destination)return;cabineActionMidi.stopLearn();directLearn=false;learnParameter=null;learnControl=-1;let index=state.routes.findIndex(r=>r.source===source&&r.destination===destination.index);if(index<0){if(state.routes.length>=64){message('Limite de 64 rotas atingido.',true);return;}index=state.routes.length;const route={source,destination:destination.index,amount:.25};state.routes.push(route);send({type:'route',index,...route});}selectedSource=null;sourcesExpanded=false;renderAddressBar();refreshAddresses();document.querySelectorAll('[data-param]').forEach(n=>{if(n.dataset.param===d.id)n.modTray?.focus(source);});message('Arraste o mini knob para cima ou para baixo para ajustar a profundidade.');}
function renderAddressBar() {
  document.documentElement.classList.toggle('modulation-addressing',selectedSource!==null||sourcesExpanded);
  document.documentElement.classList.toggle('midi-learning',directLearn||!!learnParameter||learnControl>=0);$('quick-learn')?.setAttribute('aria-pressed',directLearn||!!learnParameter||learnControl>=0);
  const root = $('address-bar'); root.replaceChildren(); if (!meta) return;
  const learn = el('button', directLearn || learnParameter ? 'active' : '', 'MIDI Learn');
  learn.onclick = () => { cabineActionMidi.stopLearn();directLearn = !directLearn; learnParameter = null; learnControl = -1; selectedSource = null; renderAddressBar(); refreshAddresses(); message(directLearn ? 'Toque no controle que deseja aprender.' : 'MIDI Learn encerrado.'); };
  const select=el('button',selectedSource!==null?'active':'',selectedSource===null?'Fontes de modulação':`${meta.sources.find(s=>s.index===selectedSource)?.name} → toque no destino`);select.setAttribute('aria-expanded',sourcesExpanded);select.onclick=()=>{sourcesExpanded=!sourcesExpanded;renderAddressBar();};
  const cancel = el('button', '', 'Cancelar'); cancel.hidden = !directLearn && !learnParameter && selectedSource === null && learnControl<0;
  cancel.onclick = () => { selectedSource = null; directLearn = false; learnParameter = null; learnControl = -1; renderAddressBar(); refreshAddresses(); message('Endereçamento encerrado.'); };
  root.append(learn, select, cancel);
  if(learnControl>=0)root.append(el('span','',`CTRL ${learnControl+1}: aguardando CC ou nota`));
  if(sourcesExpanded)root.append(sourceBank({el,metadata:()=>meta,pick:source=>{selectedSource=source;sourcesExpanded=false;directLearn=false;learnParameter=null;learnControl=-1;renderAddressBar();refreshAddresses();message('Toque no destino. A profundidade ficará no mini knob.');}}));
}
function subtabs(names, selected, callback) { const nav = el('div', 'subtabs'); names.forEach((name, i) => { const b = el('button', i === selected ? 'active' : '', name); b.onclick = () => callback(i); nav.append(b); }); return nav; }
function controlSection(root, title, items, collapsible = false) {
  const section = el(collapsible ? 'details' : 'section', 'control-section');
  section.append(el(collapsible ? 'summary' : 'h3', '', title));
  const grid = el('div', 'deep-controls'), choices = el('div', 'choice-controls');
  const categorical = d => !!controlChoices(d) || /^op\d_mode$/.test(d.id) || fxSlots.includes(d.id);
  items.forEach(d => (categorical(d) ? choices : grid).append(field(d)));
  if (choices.childElementCount) section.append(choices); if (grid.childElementCount) section.append(grid); root.append(section); return section;
}
function cancelEditorGestures(){document.querySelectorAll('.mod-route-list').forEach(n=>n.cancelRouteGestures?.());closeMidiMenu();document.querySelectorAll('[data-param]').forEach(n=>{n.cancelParameterDrag?.();n.modTray?.cancelGestures();n.cancelMidiLearnGesture?.();});document.querySelectorAll('.envelope-viewport,.function-editor,.filter-response').forEach(n=>n.cancelGraphGestures?.());}
function renderView() {
  cancelEditorGestures();
  document.querySelectorAll('.filter-response').forEach(n=>n.disposeFilterResponse?.());
  modulationFeedback.clear();
  sourceScopes.clear();
  synthPanel.close();
  closeMidiMenu();
  closeSourcePicker();
  performanceControls.sync();
  if(meta)shell.output(root=>{root.append(el('h3','','INPUT / OUTPUT'));const grid=el('div','io-controls');for(const [id,name]of [['in_gain','IN GAIN'],['in_mix','IN MIX'],['in_fx','INPUT → FX'],['level','LEVEL'],['ceiling','CEILING']])grid.append(field({...byId.get(id),name,instance:'io'}));root.append(grid,$('quality').closest('label'));});
  renderAddressBar();
  shell.park();const root = $('view');root.cancelGestures?.();root.cancelGestures=null; root.replaceChildren();root.classList.remove('cabine-sequencing','synth-view','sequence-view','fx-view');document.documentElement.dataset.activeView=view;
  if(view!=='cabine')document.documentElement.classList.remove('cabine-expanded');
  const controllerMode=view==='controller',available=Object.hasOwn(keyboardVisibility,view),noKeys=!available||!keyboardVisibility[view];shell.keyboard(!noKeys,available);document.documentElement.classList.toggle('controller-mode',controllerMode);document.documentElement.classList.toggle('no-keybed',noKeys);$('local-keys').hidden=noKeys;
  localKeyboard.sync();
  if(controllerMode){mpeController.render(root);return;}root.classList.remove('mpe-controller');
  if(view==='credits'){renderCredits(root,el);return;}
  if(view==='connections'){root.append(el('h2','','Conexões e sessões'));shell.connections(root);mappingPanel.render(root);if(meta)controlSection(root,'Performance MIDI',descriptors.filter(d=>['mpe_mode','bend_range','mpe_bend_range'].includes(d.id)));renderMidiMaps(root);return;}
  if (!meta) { root.append(el('div', 'empty-state', 'Ative o áudio para abrir os controles do instrumento.')); return; }
  if (view === 'perform') {
    const caption = el('div', 'view-caption'); caption.append(el('span', '', 'MACROS / PERFORMANCE'), el('span', '', 'ARRASTE PARA CIMA OU PARA BAIXO'));
    const knobs = el('div', 'knobs'); for (const [id, title] of [['macro1', 'MACRO 1'], ['macro2', 'MACRO 2'], ['macro3', 'MACRO 3'], ['cutoff', 'FILTRO'], ['rev_amount', 'ESPAÇO'], ['level', 'SAÍDA']]) knobs.append(knob(id, title));
    root.append(caption, knobs, el('p', 'perform-note', 'Os macros seguem os destinos do preset. Duplo toque restaura o valor padrão.'));
  } else if (view === 'operators') {
    synthPanel.render(root);
  } else if (view === 'effects') {
    fxPanel.render(root);
  } else if (view === 'modulation') renderModulation(root);
  else if (view === 'cabine') renderCabine(root);
  else if (view === 'sequence') renderSequence(root);
}
document.querySelectorAll('[data-view]').forEach(b => { b.onclick = () => { if(view==='controller')mpeController.leave();if(b.dataset.view==='controller'){directLearn=false;learnParameter=null;learnControl=-1;selectedSource=null;sourcesExpanded=false;releaseAll();if(meta)setParam('seq_play',0,true);if(context?.state==='running')context.suspend().then(()=>{$('start').textContent='Retomar áudio ↗';});}view = b.dataset.view; document.querySelectorAll('[data-view]').forEach(x => x.classList.toggle('active', x === b)); renderView(); }; });
function renderModulation(root){modPanel.render(root);}
function renderRouteList(root) {mountRouteList({root,el,state,metadata:()=>meta,send,changed:refreshAddresses});}

function renderMidiMaps(root){
  const section=el('section','midi-maps');section.append(el('h3','','MIDI Learn / Forget'),el('p','perform-note','LEARN no topo: escolha um controle e mova um CC. Botão direito ou toque longo no knob abre MIDI Learn / Forget. Pickup evita saltos.'));
  if(!state.ccMaps.length)section.append(el('p','perform-note','Nenhum mapa aprendido.'));
  for(const map of state.ccMaps){const row=el('div','midi-map'),name=el('span','',parameterCaption(byId.get(map.param))),cc=el('input'),channel=el('select'),forget=el('button','small-button','Forget');cc.type='number';cc.min=0;cc.max=127;cc.value=map.cc;cc.setAttribute('aria-label',`CC de ${name.textContent}`);for(let c=-1;c<16;++c){const o=el('option','',c<0?'ANY CH':`CH ${c+1}`);o.value=c;channel.append(o);}channel.value=map.channel;channel.setAttribute('aria-label',`Canal de ${name.textContent}`);cc.onchange=()=>{map.cc=Math.max(0,Math.min(127,Math.round(+cc.value||0)));cc.value=map.cc;pickup.reset(map.param);persistMappings();refreshAddresses();};channel.onchange=()=>{map.channel=+channel.value;pickup.reset(map.param);persistMappings();refreshAddresses();};forget.onclick=()=>{forgetMidi(map.param);renderView();};row.append(name,cc,channel,forget);section.append(row);}root.append(section);
}
function renderSequence(root){sequencePanel.render(root);}

function note(status, a, b, when = context?.currentTime) {
  if (!audioReady) { message('Ative o áudio para tocar.'); return; }
  send({ type: 'midi', status, a, b, frame: Math.round(Math.max(context.currentTime, when || 0) * context.sampleRate) });
}

function releaseAll() { cancelEditorGestures();sequencePanel.resetInput(); cabineActionMidi.cancel(); $('view').cancelGestures?.();send({ type: 'panic' }); performanceControls.reset(); state.controllerBindings.forEach((binding, index) => { if (binding?.kind === 'note') send({ type: 'control', index, value: 0 }); }); localKeyboard.stop(); }
$('panic').onclick = () => { releaseAll(); mpeController.stop(); setParam('seq_play', 0, true); if (view === 'sequence') renderView(); };
$('audition').onclick = () => { const n = selectedPreset?.note >= 24 && selectedPreset.note <= 96 ? selectedPreset.note : 48; note(0x90, n, 100); note(0x80, n, 0, context.currentTime + .65); };
function keyboard(){localKeyboard.render();}

let keyboardWidth = window.innerWidth;
window.addEventListener('resize', () => { if (window.innerWidth !== keyboardWidth) { keyboardWidth = window.innerWidth; mpeController.resize(); keyboard(); } });
installTypingKeyboard({target:window,base:()=>+$('octave').dataset.base,press:(id,n)=>localKeyboard.press(id,n),release:id=>localKeyboard.release(id),escape:()=>{cabineActionMidi.stopLearn();closeMidiMenu();directLearn=false;learnParameter=null;learnControl=-1;selectedSource=null;sourcesExpanded=false;document.querySelectorAll('.midi-actions').forEach(n=>n.remove());renderAddressBar();refreshAddresses();}});
window.addEventListener('focus',()=>modulationFeedback.suspend(document.hidden||context?.state!=='running'));
window.addEventListener('blur', ()=>{modulationFeedback.suspend(true);releaseAll();mpeController.stop();}); document.addEventListener('visibilitychange', () => { sourceScopes.suspend(document.hidden||context?.state!=='running');modulationFeedback.suspend(document.hidden||context?.state!=='running');if (document.hidden){releaseAll();mpeController.stop();} });
function handleMidi({ status, a, b, timestamp = performance.now() }) {
  if(view==='controller')return;
  performanceControls.receiveMidi(status,a,b);
  localKeyboard.receiveMidi(status,a,b);
  const kind = status & 0xf0, channel = status & 15;
  if (kind === 0xb0 && learnParameter) {
    state.ccMaps = state.ccMaps.filter(m => m.param !== learnParameter);
    if (state.ccMaps.length >= 64) message('Limite de 64 mapeamentos. Remova um mapa para aprender outro.', true);
    else { state.ccMaps.push({ cc: a, channel, param: learnParameter }); message(`${byId.get(learnParameter).name} ligado ao CC ${a}, canal ${channel + 1}.`); }
    pickup.reset();persistMappings();learnParameter = null; renderAddressBar(); refreshAddresses();
  }
  if (learnControl >= 0 && (kind === 0xb0 || kind === 0x90 && b > 0)) { state.controllerBindings[learnControl] = { kind: kind === 0xb0 ? 'cc' : 'note', number: a, channel }; persistMappings();message(`CTRL ${learnControl + 1} aprendido.`); learnControl = -1;renderAddressBar();refreshAddresses(); }
  if (kind === 0xb0) for (const map of state.ccMaps) if (map.cc === a && (map.channel===-1||map.channel === channel)) { const d = byId.get(map.param); if (d){const current=Math.pow((param(d.id)-d.min)/(d.max-d.min),d.skew||1),next=pickup.next(map,b/127,current);if(next!==null){setParam(d.id,d.min+(d.max-d.min)*Math.pow(next,1/(d.skew||1)),false,'midi');pickup.applied(map,Math.pow((param(d.id)-d.min)/(d.max-d.min),d.skew||1));}} } 
  if(kind===0xb0)refreshAddresses();
  let consumed = cabineActionMidi.handle({status,a,b,timestamp});
  state.controllerBindings.forEach((binding, index) => {
    if (!binding || binding.number !== a || binding.channel!==-1&&binding.channel !== channel) return;
    if (binding.kind === 'cc' && kind === 0xb0) send({ type: 'control', index, value: b / 127 });
    if (binding.kind === 'note' && (kind === 0x80 || kind === 0x90)) { send({ type: 'control', index, value: kind === 0x90 ? b / 127 : 0 }); consumed = true; }
  });
  if(!consumed&&audioReady){const packet=sequencePanel.midi(status,a,b,view==='sequence','input');if(!packet)consumed=true;else{status=packet[0];a=packet[1];b=packet[2];}}
  if (!consumed&&audioReady) { const when = context ? context.currentTime + Math.max(0, (timestamp - performance.now()) / 1000) : 0; note(status, a, b, when); }
}
$('midi').onclick = async () => {
  try {
    if (!midi) midi = new MidiConnection(handleMidi, text => message(text), releaseAll);
    await midi.connect(); $('midi').classList.add('midi-on');
  } catch (e) { message(e.message, true); }
};
$('ble').onclick = async () => {
  try {
    if (!midi) midi = new MidiConnection(handleMidi, text => message(text), releaseAll);
    await midi.bluetooth();
  } catch (e) { message(e.message, true); }
};
$('input').onclick = async () => {
  try {
    if (!audioReady) throw Error('Ative o áudio antes de conectar a entrada.');
    if (inputStream) { inputNode.disconnect(); inputStream.getTracks().forEach(t => t.stop()); inputStream = inputNode = null; setParam('in_mix', 0, true); $('input').textContent = 'Entrada de áudio'; return; }
    inputStream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: 2 } });
    inputNode = context.createMediaStreamSource(inputStream); inputNode.connect(node); $('input').textContent = 'Desconectar entrada'; message('Entrada conectada e muda. Abra I/O e ajuste IN MIX.');
  } catch (e) { message(e.message, true); }
};
$('save').onclick = async () => {
  if (!meta) { message('Ative o áudio antes de salvar uma sessão.'); return; }
  const text = JSON.stringify({ format: 'usina-web', version: 5, ...state }, null, 2);
  try {
    if (window.Capacitor?.isNativePlatform()) {
      const { registerPlugin } = await import('./capacitor-core.js'); const storage = registerPlugin('UsinaStorage');
      await storage.save({ text }); message('Sessão salva.'); return;
    }
    const blob = new Blob([text], { type: 'application/json' }), a = el('a'); a.href = URL.createObjectURL(blob); a.download = 'usina-session.json'; a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  } catch (e) { message(e.message, true); }
};
let importPlan = null;
$('import-sound').onchange = async e => {
  try {
    if (!meta) throw Error('Ative o áudio antes de importar um som.');
    const file = e.target.files[0]; if (!file || file.size > 2 * 1024 * 1024) throw Error('Preset inválido ou maior que 2 MB.');
    importPlan = parseVstSound(await file.text(), meta);
    $('import-name').textContent = importPlan.name;
    $('import-summary').textContent = `${Object.keys(importPlan.params).length} parâmetros de som · ${importPlan.routes.length} ${importPlan.routes.length === 1 ? 'rota' : 'rotas'} · 3 funções`;
    $('import-notices').replaceChildren(...importPlan.notices.map(n => el('li', '', n)));
    $('import-blockers').replaceChildren(...importPlan.blockers.map(n => el('li', '', n)));
    $('import-apply').disabled = importPlan.blockers.length > 0;
    $('import-dialog').showModal();
  } catch (error) { importPlan = null; message(error.message, true); }
  e.target.value = '';
};
$('import-cancel').onclick = () => { importPlan = null; $('import-dialog').close(); };
$('import-dialog').addEventListener('cancel', () => { importPlan = null; });
$('import-apply').onclick = () => {
  try {
    const next = applyVstSound(state, importPlan, meta);
    releaseAll(); pickup.reset(); pendingParams.clear(); Object.assign(state, next); selectedPreset = null;
    sendState(); shell.setPreset(state.preset); $('patch-name').textContent = state.preset; $('patch-desc').textContent = 'Som importado do desktop. Sequência e mapas MIDI preservados.';
    renderView(); renderLibrary(); importPlan = null; $('import-dialog').close(); message('Som importado. Transporte parado e realimentação em zero. Toque para ouvir.');
  } catch (error) { message(error.message, true); }
};
$('load').onchange = async e => {
  try {
    if (!meta) throw Error('Ative o áudio antes de abrir uma sessão.');
    const file = e.target.files[0]; if (!file || file.size > 2 * 1024 * 1024) throw Error('Sessão inválida ou maior que 2 MB.');
    const data = JSON.parse(await file.text()); if (data.format !== 'usina-web' || ![1,2,3,4,5].includes(data.version) || !data.params || !Array.isArray(data.routes) || data.routes.length > 64) throw Error('Formato de sessão não reconhecido.');
    const params = {}; for (const d of descriptors) { const v = data.params[d.id]; const clamped = typeof v === 'number' && Number.isFinite(v) ? Math.min(d.max, Math.max(d.min, v)) : d.default; params[d.id] = Math.min(d.max, Math.max(d.min, d.min + Math.round((clamped - d.min) / d.step) * d.step)); } params.seq_play = 0; params.feedback = 0;
    const routes = data.routes.filter(r => meta.sources.some(s => s.index === r.source) && meta.destinations.some(d => d.index === r.destination) && Number.isFinite(r.amount)).map(r => ({ source: r.source, destination: r.destination, amount: Math.min(1, Math.max(-1, r.amount)) }));
    if (!Array.isArray(data.sequence) || data.sequence.length !== 9 || !Array.isArray(data.lanes) || data.lanes.length !== 9) throw Error('Sequência inválida.');
    for (let l = 0; l < 9; ++l) { if (!Array.isArray(data.sequence[l]) || data.sequence[l].length !== 64 || !data.lanes[l] || !Number.isInteger(data.lanes[l].length) || data.lanes[l].length < 1 || data.lanes[l].length > 64) throw Error('Pista inválida.'); for (const s of data.sequence[l]) { if(s.notes!==undefined&&(!Array.isArray(s.notes)||s.notes.length>16||s.notes.some(n=>!Number.isInteger(n)||n<0||n>127)))throw Error('Acorde inválido.');if(!validDurations(s.durations))throw Error('Duração de nota inválida.');for(const key of ['accent','late','condition','pitchOffset'])if(s[key]!==undefined&&!Number.isFinite(s[key]))throw Error('Expressão do passo inválida.');for (const k of ['note', 'velocity', 'probability', 'ratchet', 'gate']) if (!Number.isFinite(s[k])) throw Error('Passo inválido.'); } }
    const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
    const sequence = data.sequence.map(l => l.map(s => ({ on: !!s.on, durations:s.durations?{...s.durations}:undefined, inherit: s.inherit === true, note: Math.round(clamp(s.note, 0, 127)), notes:Array.isArray(s.notes)?[...new Set(s.notes.map(n=>Math.round(clamp(n,0,127))))]:undefined, velocity: Math.round(clamp(s.velocity, 0, 127)), probability: clamp(s.probability, 0, 1), ratchet: Math.round(clamp(s.ratchet, 1, 8)), gate: clamp(s.gate, .01, 1), accent:clamp(s.accent??0,0,1),late:Math.round(clamp(s.late??0,-100,100)),condition:Math.round(clamp(s.condition??0,0,10)),pitchOffset:Math.round(clamp(s.pitchOffset??0,-48,48)) })));
    const lanes = data.lanes.map((l,i) => ({ length: l.length, direction: Number.isInteger(l.direction) ? clamp(l.direction, 0, 3) : 0, mul: Number.isInteger(l.mul) ? clamp(l.mul, 1, 16) : 1, div: Number.isInteger(l.div) ? clamp(l.div, 1, 16) : 1, mute: !!l.mute, mono:!!l.mono,scale:Number.isInteger(l.scale)?clamp(l.scale,-1,31):-1,root:Number.isInteger(l.root)?clamp(l.root,0,11):0, baseNote:Number.isInteger(l.baseNote)?clamp(l.baseNote,0,127):i>=5?[36,41,48,55][i-5]:60 }));
    const functions = Array.isArray(data.functions) && data.functions.length === 3 ? data.functions : cloneFunctions();
    for (const f of functions) if (!Array.isArray(f) || f.length < 2 || f.length > 64 || f.some((p, i) => !Array.isArray(p) || p.length !== 3 || p.some(v => !Number.isFinite(v)) || p[0] < 0 || p[0] > 1 || p[1] < 0 || p[1] > 1 || Math.abs(p[2]) > 1 || i > 0 && p[0] < f[i - 1][0])) throw Error('Curva de função inválida.');
    const ccMaps = Array.isArray(data.ccMaps) ? data.ccMaps.slice(0, 64).filter(m => byId.has(m.param) && Number.isInteger(m.cc) && m.cc >= 0 && m.cc <= 127 && Number.isInteger(m.channel) && m.channel >= -1 && m.channel < 16) : [];
    const controllerBindings = Array.from({ length: 8 }, (_, i) => { const b = data.controllerBindings?.[i]; return b && ['cc', 'note'].includes(b.kind) && Number.isInteger(b.number) && b.number >= 0 && b.number <= 127 && Number.isInteger(b.channel) && b.channel >= -1 && b.channel < 16 ? b : null; });
    releaseAll(); cabineActionMidi.stopLearn();pickup.reset(); pendingParams.clear(); selectedPreset = null; Object.assign(state, { params, routes, functions, ccMaps, controllerBindings, actionBindings:actionBindings(data.actionBindings),mappingPorts:data.mappingPorts||{}, sequence, lanes, cabine: cabineLayout(data.cabine), preset: typeof data.preset === 'string' ? data.preset.slice(0, 100) : 'SESSÃO', resolution: Number.isInteger(data.resolution) ? Math.min(16, Math.max(1, data.resolution)) : 4, swing: Number.isFinite(data.swing) ? Math.min(75, Math.max(0, data.swing)) : 0 });
    persistMappings();sendState();shell.setPreset(state.preset); $('patch-name').textContent = state.preset; $('patch-desc').textContent = 'Sua sessão, restaurada.'; renderView(); message('Sessão carregada. Transporte parado e realimentação em zero.');
  } catch (e) { message(e.message, true); }
  e.target.value = '';
};
const scopeData = new Float32Array(2048); let lastDraw = 0;
function draw(now) {
  requestAnimationFrame(draw); if (document.hidden || now - lastDraw < 33) return; lastDraw = now;
  const canvas = $('scope'), ratio = Math.min(2, devicePixelRatio || 1), w = Math.round(canvas.clientWidth * ratio), h = Math.round(canvas.clientHeight * ratio);
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
  const g = canvas.getContext('2d'); g.clearRect(0, 0, w, h); g.strokeStyle = '#32452b'; g.lineWidth = 1;
  g.beginPath(); for (let x = 0; x < w; x += w / 14) { g.moveTo(x, 0); g.lineTo(x, h); } for (let y = 0; y < h; y += h / 4) { g.moveTo(0, y); g.lineTo(w, y); } g.stroke();
  if (analyser) analyser.getFloatTimeDomainData(scopeData); else scopeData.fill(0);
  g.strokeStyle = '#c2f000'; g.lineWidth = 1.5 * ratio; g.beginPath();
  for (let x = 0; x < w; ++x) { const y = h * .46 - scopeData[Math.floor(x / w * scopeData.length)] * h * .7; if (x) g.lineTo(x, y); else g.moveTo(x, y); } g.stroke();
}
async function boot() {
  const response = await fetch('./library.json'); if (!response.ok) throw Error('Não foi possível carregar a biblioteca.');
  const data = await response.json(); library = [{ name: 'INIT', cat: 'init', values: {}, desc: { pt: 'Um seno. O início de qualquer coisa.' } }, ...data.patches];
  const schema=await fetch('./metadata.json');if(!schema.ok)throw Error('Metadados do instrumento indisponíveis.');meta=await schema.json();configureControls(meta);descriptors=meta.parameters.map((d,index)=>({...d,index}));byId=new Map(descriptors.map(d=>[d.id,d]));byName=new Map(descriptors.map(d=>[d.name,d]));for(const d of descriptors)state.params[d.id]??=d.default;
  const graphs=await fetch('./algorithms.json');if(!graphs.ok)throw Error('Diagramas do instrumento indisponíveis.');algorithmData=await graphs.json();
  try{const stored=localStorage.getItem('usina.mappings.v1');if(stored){const {ignored,...maps}=importMappings(stored,meta);Object.assign(state,maps);}}catch{message('Mapa salvo não pôde ser restaurado. Use IMPORT MAP para carregar outro.',true);}
  presetBrowser.setLibrary(library); renderCategories(); choosePreset(library.find(p => p.name === state.preset) || library[1], false); keyboard(); renderView(); if($('scope').getClientRects().length)requestAnimationFrame(draw);
}
boot().catch(e => message(e.message, true));
setInterval(() => { if (audioReady && context?.state==='running' && !document.hidden) send({ type: 'maintenance' }); }, 100);
const localPreview=['localhost','127.0.0.1'].includes(location.hostname)&&new URLSearchParams(location.search).has('preview');
if ('serviceWorker' in navigator && !window.Capacitor?.isNativePlatform() && !localPreview) {
  navigator.serviceWorker.register('./service-worker.js').catch(() => {});
}
