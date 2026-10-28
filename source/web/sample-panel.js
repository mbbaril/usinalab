// SPDX-License-Identifier: GPL-3.0-only
// SAMPLE (engines/fm/FmSample.h): the recording the SAMPLE operators play, on the web and the APK - LOAD a file
// (decoded by the browser, mixed to mono, up to 30 s), CAPTURE the audio input (up to 20 s; the worklet records it
// as it comes and hands it back), CLEAR. Kept on this device (IndexedDB) so the next visit plays it again; the
// region START / LENGTH lit on its waveform.
const DB='usina-sample',STORE='recording';
function idb(){return new Promise((ok,no)=>{const r=indexedDB.open(DB,1);r.onupgradeneeded=()=>r.result.createObjectStore(STORE);r.onsuccess=()=>ok(r.result);r.onerror=()=>no(r.error);});}
async function keep(rec){try{const db=await idb();await new Promise((ok,no)=>{const t=db.transaction(STORE,'readwrite');rec?t.objectStore(STORE).put(rec,'current'):t.objectStore(STORE).delete('current');t.oncomplete=ok;t.onerror=()=>no(t.error);});}catch{}}
async function kept(){try{const db=await idb();return await new Promise(ok=>{const r=db.transaction(STORE).objectStore(STORE).get('current');r.onsuccess=()=>ok(r.result||null);r.onerror=()=>ok(null);});}catch{return null;}}
export function createSamplePanel({el,send,status,param,context,ensureInput=async()=>true}){
  let rec=null,capturing=false,captureStart=0,canvas=null,label=null,capButton=null,timer=0,prefix='op1_';
  const ready=kept().then(r=>{if(r?.samples instanceof Float32Array)rec=r;return rec;});
  function give(){if(rec)send({type:'sample',samples:rec.samples.slice(),rate:rec.rate});else send({type:'sample'});}
  function take(r,name){rec=r?{samples:r.samples,rate:r.rate,name}:null;keep(rec);give();paint();}
  async function load(file){
    const ctx=context();if(!ctx){status('Ative o áudio para carregar uma gravação.',true);return;}
    try{const audio=await ctx.decodeAudioData(await file.arrayBuffer()),n=Math.min(audio.length,Math.round(30*audio.sampleRate)),mono=new Float32Array(n);
      for(let c=0;c<audio.numberOfChannels;++c){const x=audio.getChannelData(c);for(let i=0;i<n;++i)mono[i]+=x[i]/audio.numberOfChannels;}
      take({samples:mono,rate:audio.sampleRate},file.name.replace(/\.[^.]+$/,''));status(`SAMPLE: ${file.name} (${(n/audio.sampleRate).toFixed(2)} s)`);}
    catch{status(`${file.name} não pôde ser lido como áudio.`,true);}
  }
  // the worklet's answer to a stop (always one, 27/10: an empty capture used to leave the page waiting forever)
  function captured(data){capturing=false;clearInterval(timer);
    if(data?.samples){take({samples:data.samples,rate:data.rate},`CAPTURA ${new Date().toLocaleTimeString()}`);status(`SAMPLE: captura de ${(data.samples.length/data.rate).toFixed(2)} s.`);}
    else status('Nada foi gravado: a entrada de áudio não trouxe som (o microfone está liberado para o site?).',true);paint();}
  function stopCapture(){if(!capturing)return;capturing=false;clearInterval(timer);send({type:'capture',on:false});paint();}
  function paint(){
    if(capButton){capButton.textContent=capturing?`PARAR ${((performance.now()-captureStart)/1000).toFixed(1)} s`:'CAPTURAR';capButton.setAttribute('aria-pressed',String(capturing));}
    if(label)label.textContent=capturing?'gravando a entrada...':rec?`${rec.name||'gravação'}  ${(rec.samples.length/rec.rate).toFixed(2)} s`:'sem gravação';
    if(!canvas)return;const g=canvas.getContext('2d'),w=canvas.width,h=canvas.height;g.fillStyle='#0a0a0a';g.fillRect(0,0,w,h);
    const start=Math.max(0,Math.min(100,param(prefix+'b')))/100,len=Math.max(0,Math.min(200,param(prefix+'c')))/200,x0=start*w,x1=x0+(w-x0)*len;
    g.fillStyle='rgba(74,92,0,.45)';g.fillRect(x0,0,Math.max(1,x1-x0),h);
    if(rec){const s=rec.samples,cols=w,mid=h/2;for(let c=0;c<cols;++c){const a=Math.floor(c*s.length/cols),b=Math.max(a+1,Math.floor((c+1)*s.length/cols));let lo=0,hi=0;for(let i=a;i<b;++i){const v=s[i];if(v<lo)lo=v;if(v>hi)hi=v;}
      g.fillStyle=c>=x0&&c<=x1?'#c2f000':'#8a8a86';g.fillRect(c,mid-hi*mid,1,Math.max(1,(hi-lo)*mid));}}
    g.strokeStyle='#2a2a28';g.strokeRect(.5,.5,w-1,h-1);
  }
  // the operator's mode panel, when its mode is SAMPLE: the buttons, the name, the waveform (prefix: 'op3_')
  function render(root,opPrefix){
    prefix=opPrefix;
    const box=el('div','sample-box'),row=el('div','sample-buttons'),file=el('input'),loadB=el('button','small-button','CARREGAR'),clearB=el('button','small-button','LIMPAR');
    capButton=el('button','small-button','CAPTURAR');file.type='file';file.accept='audio/*';file.hidden=true;
    loadB.title='Uma gravação (WAV, MP3, OGG...: mono, até 30 s) para os operadores SAMPLE';capButton.title='Grava a entrada de áudio (até 20 s); toque de novo para parar. Fica guardada neste aparelho.';
    loadB.onclick=()=>file.click();file.onchange=()=>{if(file.files[0])load(file.files[0]);file.value='';};
    // CAPTURAR turns the audio input on first when it is off (the touch itself allows the microphone); PARAR
    // ends at once on the page - the recording comes after, by the worklet's answer
    capButton.onclick=async()=>{if(capturing){stopCapture();return;}
      if(!(await ensureInput())){status('CAPTURAR: não foi possível ligar a entrada de áudio (o microfone foi negado?).',true);return;}
      capturing=true;captureStart=performance.now();send({type:'capture',on:true});clearInterval(timer);timer=setInterval(paint,200);paint();status('Gravando a entrada de áudio: toque PARAR para terminar (até 20 s).');};
    clearB.onclick=()=>{take(null);status('SAMPLE: sem gravação.');};
    row.append(loadB,capButton,clearB,file);label=el('div','sample-name');canvas=el('canvas','sample-wave');canvas.width=360;canvas.height=64;
    box.append(row,label,canvas);root.append(box);paint();
  }
  return {render,captured,give,ready,refresh:paint};
}
