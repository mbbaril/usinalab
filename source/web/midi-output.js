// SPDX-License-Identifier: GPL-3.0-only
// One selected MIDI destination; bounded, ordered batches with cancellation and owned-note cleanup.
const canceled = () => Object.assign(Error('Envio cancelado.'), {canceled:true});
export class MidiOutput {
  constructor(onChange = () => {}) {
    this.onChange=onChange; this.ready=false; this.destinations=[]; this.queue=[]; this.count=0; this.generation=0;
    this.owned=Array.from({length:16},()=>new Set()); this.used=new Set();
  }
  async connect() {
    if (this.connecting) return this.connecting;
    this.connecting=this.initialize();
    try { return await this.connecting; } finally { this.connecting=null; }
  }
  async initialize() {
    if (window.Capacitor?.isNativePlatform()) {
      if (!this.native) {
        const {registerPlugin}=await import('./capacitor-core.js');this.native=registerPlugin('UsinaMidi');
        await this.native.addListener('devices',()=>this.refresh().catch(e=>this.onChange(e.message)));
        await this.native.addListener('output',event=>{if(event.released){this.onChange(undefined,event);return;}if(!event.ready)this.fail(Error(event.message||'Saída MIDI desconectada.'));});
      }
      await this.native.connect();
    } else {
      if(!navigator.requestMIDIAccess)throw Error('A saída MIDI exige o APK Android ou um navegador com Web MIDI.');
      this.access??=await navigator.requestMIDIAccess({sysex:false});
      this.access.onstatechange=()=>{if(this.port?.state==='disconnected')this.fail(Error('Destino MIDI removido.'));this.refresh().catch(e=>this.onChange(e.message));};
    }
    return this.refresh();
  }
  async refresh() {
    if(this.native){const result=await this.native.devices();this.destinations=(result.devices||[]).flatMap(d=>(d.inputs||[]).map(p=>({key:`${d.id}:${p.port}`,deviceId:d.id,port:p.port,name:`${d.name} · ${p.name||`porta ${p.port+1}`}${d.ble?' · BLE':''}`})));}
    else if(this.access)this.destinations=[...this.access.outputs.values()].filter(p=>p.state==='connected').map(p=>({key:p.id,name:p.name||'MIDI',browserPort:p}));
    this.onChange();return this.destinations;
  }
  invalidate() { ++this.generation;for(const record of this.queue.splice(0))record.reject(canceled());this.count=0; }
  async select(key) {
    const dest=this.destinations.find(d=>d.key===key);if(!dest)throw Error('Escolha um destino MIDI disponível.');
    await this.panic();this.ready=false;const generation=++this.generation;this.onChange();
    if(this.native)await this.native.selectOutput({deviceId:dest.deviceId,port:dest.port});
    else {if(this.port&&this.port!==dest.browserPort)await this.port.close();this.port=dest.browserPort;await this.port.open();}
    if(generation!==this.generation)throw canceled();this.selected=dest;this.ready=true;this.onChange(`Saída: ${dest.name}`);
  }
  send(messages) {
    if(!this.ready)return Promise.reject(Error('A saída MIDI não está pronta.'));
    if(!Array.isArray(messages)||!messages.length||messages.length>256||this.count+messages.length>4096){const error=Error('Fila MIDI cheia. Solte os dedos e reconecte a saída.');this.fail(error);return Promise.reject(error);}
    return new Promise((resolve,reject)=>{this.queue.push({messages:messages.map(m=>[...m]),resolve,reject,generation:this.generation});this.count+=messages.length;queueMicrotask(()=>this.drain());});
  }
  drain() {
    if(this.inflight||!this.queue.length||!this.ready)return;
    const records=[];let count=0;
    while(this.queue.length&&count+this.queue[0].messages.length<=256){const r=this.queue.shift();records.push(r);count+=r.messages.length;}
    this.count-=count;const messages=records.flatMap(r=>r.messages),generation=this.generation;
    this.inflight=(async()=>{
      try {if(this.native)await this.native.sendOutput({messages});else this.port.send(messages.flatMap(m=>(m[0]&0xf0)===0xd0?m.slice(0,2):m));this.track(messages);records.forEach(r=>r.resolve());}
      catch(error){records.forEach(r=>r.reject(error));if(generation===this.generation)this.fail(error);}
    })().finally(()=>{this.inflight=null;this.drain();});
  }
  track(messages) {
    for(const [s,a,b] of messages){const c=s&15,k=s&0xf0;if(k===0x90&&b>0){this.owned[c].add(a);this.used.add(c);}else if(k===0x80||k===0x90)this.owned[c].delete(a);else if(k===0xe0||k===0xd0||k===0xb0&&a===74)this.used.add(c);}
  }
  async panic() {
    const wasReady=this.ready;this.ready=false;this.invalidate();const generation=this.generation;this.onChange();
    await this.inflight;
    try {
      if(this.native)await this.native.panicOutput();
      else this.releaseBrowser();
    }finally{this.owned.forEach(n=>n.clear());this.used.clear();if(generation===this.generation)this.ready=wasReady;this.onChange();}
  }
  releaseBrowser() {if(!this.port)return;const bytes=[];for(const c of this.used){for(const n of this.owned[c])bytes.push(0x80+c,n,0);bytes.push(0xb0+c,123,0,0xd0+c,0,0xe0+c,0,64,0xb0+c,74,64);}if(bytes.length)this.port.send(bytes);}
  fail(error) {this.ready=false;this.invalidate();this.onChange(error.message||String(error));if(this.native)this.native.panicOutput().catch(()=>{});else {try{this.releaseBrowser();}catch{}this.owned.forEach(n=>n.clear());this.used.clear();}}
  async disconnect() {await this.panic();this.ready=false;++this.generation;if(this.native)await this.native.disconnectOutput();else if(this.port)await this.port.close();this.port=null;this.selected=null;this.onChange('Saída MIDI fechada.');}
}
