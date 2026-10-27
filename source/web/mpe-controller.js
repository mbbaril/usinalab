// SPDX-License-Identifier: GPL-3.0-only
import { MpeEncoder, pitchCoordinate } from './mpe.js';
import { MidiOutput } from './midi-output.js';
const clamp01 = v => Math.min(1, Math.max(0, v));
const names = ['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'];
export function createMpeController({el, status}) {
  let mounted, surface, destination, connectButton, stateLabel, armed=false, pending=false, octave=48, timbre=64, frame=0;
  const fingers=new Map(), moves=new Map(), markers=new Map();
  const encoder=new MpeEncoder(packets=>output.send(packets).then(()=>true).catch(error=>{if(!error.canceled)status(error.message,true);return false;}));
  const output=new MidiOutput((text,event)=>{
    if(!output.ready||event?.released){encoder.clear();fingers.clear();moves.clear();if(frame)cancelAnimationFrame(frame);frame=0;}
    if(!output.ready)armed=false;if(text)status(text);update();
  });
  function update() {
    if(!mounted)return;
    const selected=destination.value||output.selected?.key||'';
    const signature=JSON.stringify(output.destinations.map(d=>[d.key,d.name]));
    if(destination.dataset.signature!==signature){destination.replaceChildren(el('option','','Escolha o destino'));destination.options[0].value='';for(const d of output.destinations){const option=el('option','',d.name);option.value=d.key;destination.append(option);}destination.value=selected;destination.dataset.signature=signature;}
    connectButton.disabled=pending||!destination.value;
    const ready=armed&&output.ready&&!pending;mounted.dataset.ready=String(ready);
    stateLabel.textContent=ready?`${encoder.voices.size} ${encoder.voices.size===1?'dedo':'dedos'} · canais 2–16 · ±${encoder.range} st`:'Escolha e conecte uma saída MIDI para tocar.';
    const playing=new Set([...encoder.voices.values()].map(v=>v.note));surface.querySelectorAll('.mpe-key').forEach(key=>{key.disabled=!ready;key.classList.toggle('held',playing.has(+key.dataset.note));});
    for(const [id,marker] of markers)if(!ready||!fingers.has(id)||!encoder.voices.has(id)){marker.remove();markers.delete(id);}
    if(ready){const r=surface.getBoundingClientRect();for(const [id,f] of fingers){const voice=encoder.voices.get(id);if(!voice)continue;let marker=markers.get(id);if(!marker){marker=el('span','mpe-contact',String(voice.channel+1));surface.append(marker);markers.set(id,marker);}marker.style.transform=`translate(${f.x-r.left}px,${f.y-r.top}px)`;}}
  }
  function flushMoves(){frame=0;for(const [id,values] of moves)encoder.expression(id,values);moves.clear();update();}
  async function stop(restore=true) {
    const wasArmed=armed;armed=false;encoder.clear();fingers.clear();moves.clear();if(frame)cancelAnimationFrame(frame);frame=0;update();
    try{await output.panic();if(restore&&wasArmed&&output.ready)armed=true;}catch(error){status(error.message,true);}update();
  }
  function renderKeys() {
    surface.replaceChildren();markers.clear();const white=[0,2,4,5,7,9,11];
    for(let i=0;i<12;++i){const key=el('button',`mpe-key${white.includes(i)?'':' black'}`,names[i]);key.dataset.note=octave+i;key.setAttribute('aria-label',`MPE ${names[i]}${octave/12-1}`);
      if(!white.includes(i)){const before=white.filter(v=>v<i).length,width=100/7*.65;key.style.left=`${before/7*100-width/2}%`;key.style.width=`${width}%`;}
      key.onpointerdown=e=>{
        if(!armed||!output.ready||pending||e.pointerType==='mouse'&&e.button!==0)return;e.preventDefault();key.setPointerCapture(e.pointerId);
        const centres=[...surface.querySelectorAll('.mpe-key')].map(k=>{const b=k.getBoundingClientRect();return {x:b.left+b.width/2,note:+k.dataset.note};}).sort((a,b)=>a.x-b.x),r=surface.getBoundingClientRect();
        fingers.set(e.pointerId,{start:pitchCoordinate(e.clientX,centres),centres,top:r.top,height:r.height,x:e.clientX,y:e.clientY});
        encoder.begin(e.pointerId,octave+i,100,clamp01(1-(e.clientY-r.top)/r.height),timbre/127);update();
      };
      key.onpointermove=e=>{const f=fingers.get(e.pointerId);if(!f||!encoder.voices.has(e.pointerId))return;e.preventDefault();f.x=e.clientX;f.y=e.clientY;moves.set(e.pointerId,{semitones:pitchCoordinate(e.clientX,f.centres)-f.start,pressure:clamp01(1-(e.clientY-f.top)/f.height)});if(!frame)frame=requestAnimationFrame(flushMoves);};
      const end=e=>{if(!fingers.has(e.pointerId))return;if(moves.has(e.pointerId)){encoder.expression(e.pointerId,moves.get(e.pointerId));moves.delete(e.pointerId);}fingers.delete(e.pointerId);encoder.end(e.pointerId);update();};
      key.onpointerup=key.onpointercancel=key.onlostpointercapture=end;
      key.onkeydown=e=>{if((e.code==='Space'||e.code==='Enter')&&!e.repeat&&armed&&output.ready){e.preventDefault();encoder.begin(`key-${i}`,octave+i,100,.5,timbre/127);update();}};
      key.onkeyup=e=>{if(e.code==='Space'||e.code==='Enter'){e.preventDefault();encoder.end(`key-${i}`);update();}};
      surface.append(key);
    }
    update();
  }
  function render(root) {
    if(mounted)stop(false);
    mounted=root;root.classList.add('mpe-controller');
    root.append(el('h2','','Controlador MPE'),el('p','perform-note','O som vem do destino MIDI. Não é necessário ativar o áudio do celular.'));
    const connection=el('div','mpe-connection'),discover=el('button','small-button','Encontrar saídas');destination=el('select');destination.id='mpe-destination';destination.setAttribute('aria-label','Destino MIDI MPE');connectButton=el('button','small-button','Conectar saída');connectButton.id='mpe-connect';
    const close=el('button','small-button','Fechar saída');
    discover.onclick=async()=>{try{await output.connect();status(output.destinations.length?'Escolha uma porta de destino.':'Nenhuma saída disponível. Para o computador por USB, selecione MIDI na configuração USB do Android. Para um instrumento BLE, conecte-o por MIDI BLE.');}catch(error){status(error.message,true);}};
    destination.onchange=update;
    connectButton.onclick=async()=>{pending=true;const key=destination.value;await stop(false);pending=true;update();try{await output.select(key);armed=(await encoder.setup())!==false&&output.ready;if(armed)status('MPE conectado. No plugin, habilite MPE INPUT → LOWER ZONE.');}catch(error){armed=false;if(!error.canceled)status(error.message,true);}finally{pending=false;update();}};
    close.onclick=async()=>{await stop(false);try{await output.disconnect();}catch(error){status(error.message,true);}};
    connection.append(discover,destination,connectButton,close);root.append(connection);
    const settings=el('div','mpe-settings'),octaveLabel=el('label','','Oitava '),octaveSelect=el('select');octaveSelect.setAttribute('aria-label','Oitava MPE');
    for(const value of [36,48,60,72]){const option=el('option','',String(value/12-1));option.value=value;octaveSelect.append(option);}octaveSelect.value=octave;
    octaveSelect.onchange=async()=>{await stop();octave=+octaveSelect.value;renderKeys();};octaveLabel.append(octaveSelect);
    const rangeLabel=el('label','','Bend por nota ± st '),range=el('input');range.id='mpe-range';range.type='number';range.min=1;range.max=96;range.step=1;range.value=encoder.range;
    range.onchange=async()=>{const ready=armed;await stop(false);encoder.range=Math.min(96,Math.max(1,Math.round(+range.value||48)));range.value=encoder.range;if(ready&&output.ready)armed=(await encoder.setup())!==false;update();};rangeLabel.append(range);
    const release=el('button','small-button','Soltar dedos');release.id='mpe-panic';release.onclick=()=>stop();settings.append(octaveLabel,rangeLabel,release);root.append(settings);
    stateLabel=el('p','mpe-state');stateLabel.id='mpe-state';root.append(stateLabel);
    surface=el('div','mpe-surface');surface.id='mpe-surface';surface.setAttribute('aria-label','Teclado controlador MPE');root.append(surface);
    const timbreLabel=el('label','mpe-timbre','TIMBRE / CC74 '),timbreValue=el('output','',String(timbre)),slider=el('input');slider.type='range';slider.min=0;slider.max=127;slider.step=1;slider.value=timbre;slider.id='mpe-timbre';
    slider.oninput=()=>{timbre=+slider.value;timbreValue.textContent=String(timbre);if(armed&&output.ready)for(const id of encoder.voices.keys())encoder.expression(id,{timbre:timbre/127});};timbreLabel.append(timbreValue,slider);root.append(timbreLabel);
    root.append(el('p','perform-note','Horizontal: bend da nota segurada. Suba o dedo: mais pressão. CC74/timbre fica no controle separado. Cada dedo ocupa um canal próprio.'));
    root.append(el('p','perform-note','USB para computador: use o modo MIDI do Android. BLE aqui envia a instrumentos já conectados; anunciar o celular como periférico BLE para o computador ainda não está disponível.'));
    renderKeys();
  }
  return {render,stop,leave(){mounted=null;stop(false);},resize(){if(fingers.size)stop();}};
}
