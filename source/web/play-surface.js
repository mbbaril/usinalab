// SPDX-License-Identifier: GPL-3.0-only
// PLAY (docs/PLAY.md): the sound played, not edited - the organ's manuals or the grid, the four macros, the snapshots
// A..D and the XY between them (REC: a gesture looped), HOLD, LOCK, the velocity by the touch's height. Two
// destinations: HERE (this app's engine) and CONTROL (a MIDI output: the plugin on the computer, another instance,
// another instrument - MPE per finger when on, the macros as CC 16..19 on channel 1). The pure rules: play-model.js.
import {whitesFor,organKeys,organNoteAt,gridPitch,velocityAt,mixSnapshots,easeGlide,loopAt,closeLoop,macroCc,parsePlayState,
  noteName,maxManuals,CORNERS,GLIDES,GRID_COLS,GRID_ROWS,MACRO_CC} from './play-model.js';
import {cabineScales} from './cabine-keys.js';
import {MpeEncoder,pitchCoordinate} from './mpe.js';
import {MidiOutput} from './midi-output.js';
const KEY='usina.play';
export function createPlaySurface({el,status,local}){
  let s=load(),root=null,keysBox=null,xyBox=null,frame=0,glide=null,looping=false,recording=null,armed=false,hold=false,gestureOn=false,syncOptions=()=>{};
  const touches=new Map(),counts=new Map(),latched=new Set(),macroEls=[],snapEls=[];
  const output=new MidiOutput((text,event)=>{if(event?.released||!output.ready)encoder.clear();if(text)status(text);paintHead();});
  const encoder=new MpeEncoder(packets=>output.send(packets).then(()=>true).catch(e=>{if(!e.canceled)status(e.message,true);return false;}));
  // HERE's BEND: the engine's own MPE (a member channel a finger, its range set as it starts; channel 1 stays the
  // keyboard of the CABINE and the sequencer - src/engines/fm/PerformanceMidi.h)
  const localEncoder=new MpeEncoder(packets=>{for(const[st,a,b]of packets)local.note(st,a,b);return true;},{configureChannel:true});
  let mpeReady=false;
  function load(){try{return parsePlayState(JSON.parse(localStorage.getItem(KEY)||'null'));}catch{return parsePlayState(null);}}
  function save(){try{localStorage.setItem(KEY,JSON.stringify(s));}catch{}}
  const control=()=>s.dest==='control',sendOut=m=>{if(output.ready)output.send(m).catch(e=>{if(!e.canceled)status(e.message,true);});};
  // the slide: LEGATO (each key passed a note; their releases add up) or BEND (one note a finger, bent to where it is:
  // one voice, the phone's default). BEND needs a note's own channel: MPE out (CONTROL) or the engine's MPE (HERE)
  const slideBend=()=>(s.slide??((root?.clientWidth||1280)<900?'bend':'legato'))==='bend';
  const viaMpe=()=>control()?s.mpe&&mpeReady:slideBend(),enc=()=>control()?encoder:localEncoder;
  const bending=()=>slideBend()&&viaMpe();
  function ensureLocalMpe(){if(!control()&&slideBend()&&local.mpe&&!local.mpe()){local.setMpe(true);status('BEND: o MPE do motor foi ligado - cada dedo com o seu bend; o canal 1 segue como teclado.');}}
  // --- the notes: counted (two fingers on one note: one on, the last off), HOLD's latched; through MPE, a voice a finger
  function noteOn(id,n,v,pressure){
    if(viaMpe()){if(!control())ensureLocalMpe();enc().begin(id,n,v,pressure,.5);mark();return;}
    if((counts.get(n)||0)===0){if(control())sendOut([[0x90,n,v]]);else local.note(0x90,n,v);}
    counts.set(n,(counts.get(n)||0)+1);mark();
  }
  function noteOff(id,n){
    if(viaMpe()){enc().end(id);mark();return;}
    const c=(counts.get(n)||1)-1;if(c>0)counts.set(n,c);else{counts.delete(n);if(control())sendOut([[0x80,n,0]]);else local.note(0x80,n,0);}mark();
  }
  const sounding=n=>counts.has(n)||[...encoder.voices.values(),...localEncoder.voices.values()].some(v=>v.note===n);
  function releasePointers(){for(const[id,t]of[...touches])noteOff(id,t.note);touches.clear();}
  function releaseAll(){releasePointers();for(const[id,n]of[...latched])noteOff(id,n);latched.clear();}
  // --- where a pointer is: a manual (or the grid) and its key
  const whites=()=>whitesFor(keysBox?.clientWidth||root?.clientWidth||1280);
  // (the manuals that fit: the width's limit, and at least ~120 px of height each - the phone lying down shows one)
  function manualsShown(){const h=keysBox?.clientHeight||600;return s.layout==='grid'?1:Math.max(1,Math.min(s.manuals,maxManuals(root?.clientWidth||1280),Math.floor((h+6)/126)));}
  function hit(e){
    const r=keysBox.getBoundingClientRect(),n=manualsShown(),gap=6,mh=(r.height-gap*(n-1))/n,y=e.clientY-r.top;
    const m=n-1-Math.min(n-1,Math.max(0,Math.floor(y/(mh+gap)))),top=(n-1-m)*(mh+gap),fy=(y-top)/mh,fx=(e.clientX-r.left)/r.width;
    if(s.layout==='grid'){const col=Math.floor(fx*GRID_COLS),row=Math.floor(fy*GRID_ROWS);if(fx<0||fx>=1||fy<0||fy>=1)return null;
      return {note:gridPitch(row,col,{scale:s.scale,root:s.root,octave:s.gridOctave}),rel:fy*GRID_ROWS-row,manual:0};}
    const k=organNoteAt(s.octaves[m],fx,fy,whites());return k&&{...k,manual:m};
  }
  function centres(manual){const r=keysBox.getBoundingClientRect();return organKeys(s.octaves[manual],whites()).map(k=>({x:r.left+(k.x+k.w/2)*r.width,note:k.note})).sort((a,b)=>a.x-b.x);}
  function down(e){
    if(e.pointerType==='mouse'&&e.button!==0)return;const h=hit(e);if(!h||h.note<0)return;e.preventDefault();keysBox.setPointerCapture(e.pointerId);
    if(hold&&!touches.size&&latched.size){for(const[id,n]of[...latched])noteOff(id,n);latched.clear();}
    const v=velocityAt(h.rel,s.vel),mpe=viaMpe();
    touches.set(e.pointerId,{note:h.note,velocity:v,manual:h.manual,start:mpe&&s.layout==='organ'?pitchCoordinate(e.clientX,centres(h.manual)):h.note});
    noteOn(e.pointerId,h.note,v,mpe?1-h.rel:0);
  }
  function move(e){
    const t=touches.get(e.pointerId);if(!t)return;e.preventDefault();
    const h=hit(e);
    if(bending()){                                     // (BEND: the finger's slide bends its note; its height the pressure)
      const semis=s.layout==='organ'?pitchCoordinate(e.clientX,centres(t.manual))-t.start:0;
      enc().expression(e.pointerId,{semitones:semis,...(h?{pressure:1-h.rel}:{})});return;
    }
    if(!h||h.note<0||h.note===t.note){if(h&&viaMpe())enc().expression(e.pointerId,{pressure:1-h.rel});return;}   // (off the keys: the note goes on, as an organ's)
    const old=t.note;t.note=h.note;
    if(viaMpe()){t.start=h.note;enc().begin(e.pointerId,h.note,t.velocity,1-h.rel,.5);mark();return;}   // (LEGATO through MPE: the finger's voice moves to the new key)
    noteOn(e.pointerId,h.note,t.velocity,0);noteOff(e.pointerId,old);   // (a run: legato, the new note in before the old one out)
  }
  function up(e){
    const t=touches.get(e.pointerId);if(!t)return;touches.delete(e.pointerId);
    if(hold)latched.add([e.pointerId,t.note]);else noteOff(e.pointerId,t.note);
  }
  function mark(){if(!frame)frame=requestAnimationFrame(paintKeys);}
  function paintKeys(){frame=0;keysBox?.querySelectorAll('[data-note]').forEach(k=>k.classList.toggle('on',sounding(+k.dataset.note)));}
  function renderKeys(){
    keysBox.replaceChildren();const n=manualsShown();keysBox.dataset.layout=s.layout;keysBox.style.setProperty('--manuals',n);octBox.style.setProperty('--manuals',n);
    if(s.layout==='grid'){
      const grid=el('div','play-grid');for(let row=0;row<GRID_ROWS;++row)for(let col=0;col<GRID_COLS;++col){const p=gridPitch(row,col,{scale:s.scale,root:s.root,octave:s.gridOctave}),pad=el('div',`play-pad${p>=0&&(p-s.root)%12===0?' root':''}`,p>=0?noteName(p):'');pad.dataset.note=p;grid.append(pad);}
      keysBox.append(grid);
    } else for(let m=n-1;m>=0;--m){
      const man=el('div','play-manual');for(const k of organKeys(s.octaves[m],whites())){const key=el('div',k.black?'play-key black':'play-key',!k.black&&k.note%12===0?noteName(k.note):'');key.dataset.note=k.note;key.style.left=`${k.x*100}%`;key.style.width=`${k.w*100}%`;man.append(key);}
      keysBox.append(man);
    }
    paintKeys();renderOctaves();
  }
  // --- the octaves: a strip beside the keys, a +/- pair a manual
  let octBox=null;
  function renderOctaves(){
    octBox.replaceChildren();const n=manualsShown();
    for(let m=n-1;m>=0;--m){const box=el('div','play-oct'),plus=el('button','','+'),minus=el('button','','−'),low=s.layout==='grid'?(s.gridOctave+2)*12+s.root:(s.octaves[m]+2)*12;
      const shift=d=>{releasePointers();if(s.layout==='grid')s.gridOctave=Math.min(8,Math.max(0,s.gridOctave+d));else s.octaves[m]=Math.min(8,Math.max(0,s.octaves[m]+d));save();renderKeys();};
      plus.onclick=()=>shift(1);minus.onclick=()=>shift(-1);plus.setAttribute('aria-label','Oitava acima');minus.setAttribute('aria-label','Oitava abaixo');
      box.append(plus,el('output','',noteName(low)),minus);octBox.append(box);}
  }
  // --- the macros: four bipolar strips; the snapshots; the XY
  function macroValue(i){return control()?s.macros?.[i]??0:local.macro(i);}
  function setMacros(values){
    s.macros=values.slice();
    if(control())sendOut(values.map((v,i)=>[0xb0,MACRO_CC+i,macroCc(v)]));else values.forEach((v,i)=>local.setMacro(i,v));
    paintMacros();
  }
  function applyXY(x,y){s.x=Math.min(1,Math.max(0,x));s.y=Math.min(1,Math.max(0,y));setMacros(mixSnapshots(s.snaps,s.set,s.x,s.y));paintXY();}
  function paintMacros(){macroEls.forEach((m,i)=>{const v=macroValue(i);m.style.setProperty('--v',v);m.querySelector('output').textContent=`${v>0?'+':''}${Math.round(v*100)}`;});}
  function paintSnaps(){snapEls.forEach((b,i)=>b.setAttribute('aria-pressed',String(!!((s.set>>i)&1))));}
  function paintXY(){if(!xyBox)return;const dot=xyBox.querySelector('.play-xy-dot');dot.style.left=`${s.x*100}%`;dot.style.top=`${s.y*100}%`;xyBox.classList.toggle('armed',armed);xyBox.classList.toggle('looping',looping);
    xyBox.querySelectorAll('.play-corner').forEach((c,i)=>c.classList.toggle('set',!!((s.set>>i)&1)));
    const path=xyBox.querySelector('polyline');path.setAttribute('points',s.loop.map(p=>`${(p.x*100).toFixed(1)},${(p.y*100).toFixed(1)}`).join(' '));
    root?.querySelector('#play-rec')?.setAttribute('aria-pressed',String(armed));root?.querySelector('#play-loop')?.setAttribute('aria-pressed',String(looping));}
  function store(i){if(s.lock)return;s.snaps[i]=[0,1,2,3].map(macroValue);s.set|=1<<i;s.x=CORNERS[i][0];s.y=CORNERS[i][1];save();paintSnaps();paintXY();}
  function recall(i){
    looping=false;const secs=GLIDES[s.glide];
    if(!secs){applyXY(...CORNERS[i]);save();return;}
    glide={from:[s.x,s.y],to:CORNERS[i],start:performance.now(),secs};tick();
  }
  function tick(){
    if(glide){const t=Math.min(1,(performance.now()-glide.start)/1000/glide.secs),e=easeGlide(t);applyXY(glide.from[0]+(glide.to[0]-glide.from[0])*e,glide.from[1]+(glide.to[1]-glide.from[1])*e);if(t>=1){glide=null;save();}}
    else if(looping&&s.loop.length>1){const p=loopAt(s.loop,(performance.now()-loopStart)/1000);applyXY(p.x,p.y);}
    if((glide||looping)&&root)requestAnimationFrame(tick);
  }
  let loopStart=0;
  function xyPointer(e,phase){
    const r=xyBox.getBoundingClientRect(),x=(e.clientX-r.left)/r.width,y=(e.clientY-r.top)/r.height;
    if(phase==='down'){e.preventDefault();xyBox.setPointerCapture(e.pointerId);glide=null;looping=false;gestureOn=true;if(armed&&!s.lock)recording={start:performance.now(),points:[]};}
    if(!gestureOn)return;applyXY(x,y);if(recording)recording.points.push({t:(performance.now()-recording.start)/1000,x:s.x,y:s.y});
    if(phase==='up'){gestureOn=false;if(recording){const loop=closeLoop(recording.points);recording=null;armed=false;if(loop){s.loop=loop;looping=true;loopStart=performance.now();tick();}}save();paintXY();}
  }
  // --- the head: HERE / CONTROL, the MIDI output, MPE
  let head=null;
  function paintHead(){
    if(!head)return;head.querySelector('#play-here').setAttribute('aria-pressed',String(!control()));head.querySelector('#play-control').setAttribute('aria-pressed',String(control()));
    const out=head.querySelector('.play-output');out.hidden=!control();if(!control())return;
    const sel=out.querySelector('select'),cur=sel.value||output.selected?.key||'',sig=JSON.stringify(output.destinations.map(d=>[d.key,d.name]));
    if(sel.dataset.sig!==sig){sel.replaceChildren(el('option','','Escolha o destino'));sel.options[0].value='';for(const d of output.destinations){const o=el('option','',d.name);o.value=d.key;sel.append(o);}sel.value=cur;sel.dataset.sig=sig;}
    out.querySelector('#play-mpe').setAttribute('aria-pressed',String(s.mpe));
    out.querySelector('.play-link').textContent=output.ready?(s.mpe?`MPE · ${output.selected?.name}`:output.selected?.name):'sem saída';
  }
  async function connect(key){
    releaseAll();try{await output.select(key);mpeReady=s.mpe?(await encoder.setup())!==false:false;status(s.mpe?'MPE conectado. No plugin: MPE INPUT → LOWER ZONE; macros em CC 16–19 (MIDI LEARN uma vez).':'Saída conectada. Macros em CC 16–19, canal 1.');}
    catch(e){mpeReady=false;if(!e.canceled)status(e.message,true);}paintHead();
  }
  // compact: the strip hidden but the macros (beside the keys, as wheels) - a short screen's default (the phone lying
  // down: two manuals fit); PAINEL brings the whole strip back
  // (and keys: only the keys, the whole width)
  const panelMode=()=>s.panel??(window.innerHeight<520?'compact':'full');
  function showPanel(){const m=panelMode(),b=head?.querySelector('#play-panel');root.classList.toggle('compact',m==='compact');root.classList.toggle('keys-only',m==='keys');if(b)b.textContent={full:'PAINEL: TUDO',compact:'PAINEL: MACROS',keys:'PAINEL: TECLAS'}[m];}
  function applyCompact(){if(!root)return;showPanel();releasePointers();requestAnimationFrame(()=>{if(root){renderKeys();syncOptions();}});}
  function renderHead(){
    head=el('div','play-head');const here=el('button','play-toggle','ESTE APP'),ctl=el('button','play-toggle','SAÍDA MIDI');here.id='play-here';ctl.id='play-control';
    here.title='O som sai deste app (o motor do Usina aqui).';ctl.title='As notas e as macros saem por MIDI: o plugin no computador, outra instância, outro instrumento.';
    here.onclick=()=>{releaseAll();s.dest='here';save();ensureLocalMpe();paintHead();paintMacros();syncOptions();};
    ctl.onclick=async()=>{releaseAll();s.dest='control';save();paintHead();try{await output.connect();}catch(e){status(e.message,true);}paintHead();};
    const out=el('div','play-output'),find=el('button','small-button','Saídas'),sel=el('select'),mpe=el('button','play-toggle','MPE'),close=el('button','small-button','Fechar'),link=el('span','play-link');mpe.id='play-mpe';
    sel.setAttribute('aria-label','Destino MIDI');find.onclick=async()=>{try{await output.connect();status(output.destinations.length?'Escolha o destino.':'Nenhuma saída. Para o computador por USB: modo MIDI na configuração USB do Android.');}catch(e){status(e.message,true);}paintHead();};
    sel.onchange=()=>{if(sel.value)connect(sel.value);};
    mpe.onclick=async()=>{releaseAll();s.mpe=!s.mpe;save();if(output.ready&&output.selected)await connect(output.selected.key);paintHead();};
    close.onclick=async()=>{releaseAll();try{await output.disconnect();}catch(e){status(e.message,true);}mpeReady=false;paintHead();};
    out.append(find,sel,mpe,close,link);head.append(el('span','play-dest-label','TOCA EM'));
    const panel=el('button','play-toggle','');panel.id='play-panel';panel.title='PAINEL: TUDO (instantâneos, XY, opções), MACROS (as macros ao lado do teclado) ou TECLAS (só o teclado)';
    panel.onclick=()=>{const order=['full','compact','keys'];s.panel=order[(order.indexOf(panelMode())+1)%3];save();applyCompact();};
    head.append(here,ctl,panel,out);return head;
  }
  // --- the panel
  function render(target){
    if(root)leave();root=target;root.classList.add('play-view');
    root.append(renderHead());
    const perf=el('div','play-perf'),macros=el('div','play-macros');macroEls.length=0;snapEls.length=0;
    for(let i=0;i<4;++i){const m=el('div','play-macro'),strip=el('div','play-strip'),fill=el('span','play-fill');strip.append(fill);m.append(el('label','',`MACRO ${i+1}`),strip,el('output','','0'));
      const set=e=>{const r=strip.getBoundingClientRect(),v=Math.min(1,Math.max(-1,1-2*(e.clientY-r.top)/r.height)),vals=[0,1,2,3].map(macroValue);vals[i]=v;glide=null;looping=false;setMacros(vals);};
      strip.onpointerdown=e=>{e.preventDefault();strip.setPointerCapture(e.pointerId);set(e);};strip.onpointermove=e=>{if(strip.hasPointerCapture(e.pointerId))set(e);};
      strip.ondblclick=()=>{const vals=[0,1,2,3].map(macroValue);vals[i]=0;setMacros(vals);};
      macroEls.push(m);macros.append(m);}
    const snaps=el('div','play-snaps');['A','B','C','D'].forEach((name,i)=>{const b=el('button','play-snap',name);b.title=`${name}: toque vai até ele; vazio, guarda. Toque longo ou clique-direito: guarda de novo.`;let timer=0,long=false;
      b.onpointerdown=()=>{long=false;timer=setTimeout(()=>{long=true;store(i);},550);};b.onpointerup=b.onpointercancel=()=>clearTimeout(timer);
      b.oncontextmenu=e=>{e.preventDefault();store(i);};b.onclick=()=>{if(long)return;if((s.set>>i)&1)recall(i);else store(i);};snapEls.push(b);snaps.append(b);});
    const glideSel=el('select');glideSel.setAttribute('aria-label','GLIDE');GLIDES.forEach((g,i)=>{const o=el('option','',g?`${g} s`:'0');o.value=i;glideSel.append(o);});glideSel.value=s.glide;glideSel.onchange=()=>{s.glide=+glideSel.value;save();};
    const glideLabel=el('label','play-glide','GLIDE ');glideLabel.append(glideSel);snaps.append(glideLabel);
    xyBox=el('div','play-xy');const svg=document.createElementNS('http://www.w3.org/2000/svg','svg');svg.setAttribute('viewBox','0 0 100 100');svg.setAttribute('preserveAspectRatio','none');svg.append(document.createElementNS('http://www.w3.org/2000/svg','polyline'));
    xyBox.append(svg,el('span','play-xy-dot'));['A','B','C','D'].forEach((n,i)=>{const c=el('span',`play-corner c${i}`,n);xyBox.append(c);});
    xyBox.onpointerdown=e=>xyPointer(e,'down');xyBox.onpointermove=e=>{if(xyBox.hasPointerCapture(e.pointerId))xyPointer(e,'move');};xyBox.onpointerup=xyBox.onpointercancel=e=>xyPointer(e,'up');
    const xyTools=el('div','play-xy-tools'),rec=el('button','play-toggle','REC'),loopB=el('button','play-toggle','LOOP');rec.id='play-rec';loopB.id='play-loop';
    rec.onclick=()=>{if(s.lock)return;armed=!armed;paintXY();};loopB.onclick=()=>{if(s.loop.length<2)return;looping=!looping;if(looping){glide=null;loopStart=performance.now();tick();}paintXY();};xyTools.append(rec,loopB);
    const opts=el('div','play-opts'),holdB=el('button','play-toggle','HOLD'),lockB=el('button','play-toggle','LOCK'),organB=el('button','play-toggle','ORGAN'),gridB=el('button','play-toggle','GRID'),velB=el('button','play-toggle','VEL'),bendB=el('button','play-toggle','BEND');bendB.title='BEND: deslizar entorta a nota do dedo (uma voz). Desligado: LEGATO, cada tecla uma nota.';
    const sync=()=>{holdB.setAttribute('aria-pressed',String(hold));lockB.setAttribute('aria-pressed',String(s.lock));organB.setAttribute('aria-pressed',String(s.layout==='organ'));gridB.setAttribute('aria-pressed',String(s.layout==='grid'));velB.setAttribute('aria-pressed',String(s.vel));bendB.setAttribute('aria-pressed',String(slideBend()));
      manualRow.hidden=s.layout==='grid';scaleRow.hidden=s.layout!=='grid';manualRow.querySelectorAll('button').forEach((b,i)=>{b.setAttribute('aria-pressed',String(manualsShown()===i+1));b.hidden=i+1>maxManuals(root?.clientWidth||1280);});};
    holdB.onclick=()=>{hold=!hold;if(!hold){for(const[id,n]of[...latched])noteOff(id,n);latched.clear();}sync();};
    lockB.onclick=()=>{s.lock=!s.lock;if(s.lock)armed=false;save();sync();paintXY();};
    organB.onclick=()=>{releasePointers();s.layout='organ';save();sync();renderKeys();};gridB.onclick=()=>{releasePointers();s.layout='grid';save();sync();renderKeys();};
    velB.onclick=()=>{s.vel=!s.vel;save();sync();};
    bendB.onclick=()=>{releaseAll();s.slide=slideBend()?'legato':'bend';save();ensureLocalMpe();sync();};
    const manualRow=el('div','play-row');manualRow.append(el('span','','MANUAIS'));[1,2,3].forEach(n=>{const b=el('button','play-toggle',String(n));b.onclick=()=>{releasePointers();s.manuals=n;save();sync();renderKeys();};manualRow.append(b);});
    const scaleRow=el('div','play-row'),scale=el('select'),rootSel=el('select');scale.setAttribute('aria-label','Escala');rootSel.setAttribute('aria-label','Tônica');
    cabineScales.forEach(([name],i)=>{const o=el('option','',name);o.value=i;scale.append(o);});['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'].forEach((n,i)=>{const o=el('option','',n);o.value=i;rootSel.append(o);});
    scale.value=s.scale;rootSel.value=s.root;scale.onchange=()=>{releasePointers();s.scale=+scale.value;save();renderKeys();};rootSel.onchange=()=>{releasePointers();s.root=+rootSel.value;save();renderKeys();};scaleRow.append(scale,rootSel);
    opts.append(holdB,lockB,organB,gridB,velB,bendB,manualRow,scaleRow);
    perf.append(macros,snaps,xyBox,xyTools,opts);root.append(perf);
    const keysWrap=el('div','play-keys-wrap');octBox=el('div','play-octs');keysBox=el('div','play-keys');keysBox.setAttribute('aria-label','Teclado PLAY');
    keysBox.onpointerdown=down;keysBox.onpointermove=move;keysBox.onpointerup=keysBox.onpointercancel=keysBox.onlostpointercapture=up;
    keysWrap.append(octBox,keysBox);root.append(keysWrap);showPanel();
    syncOptions=sync;sync();renderKeys();paintMacros();paintSnaps();paintXY();paintHead();
  }
  function leave(){releasePointers();looping=false;glide=null;armed=false;root?.classList.remove('play-view');root=null;keysBox=null;xyBox=null;head=null;}
  return {render,leave,releaseAll,resize(){if(root)applyCompact();},refresh(){paintMacros();},
    stopOutput:async()=>{releaseAll();try{await output.panic();}catch{}}};
}
