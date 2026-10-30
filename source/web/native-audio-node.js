// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Mario Brandalise Baril
import {NativeAudioTransport} from './native-audio-transport.js';

/** Worklet message facade. Not an AudioNode: native audio goes directly to the native stream, never context.destination. */
export class NativeAudioNode {
  constructor({plugin, metadata, sampleRate, capabilities = {}, oversampling = 4, intervalMs = 34, autoPoll = true,
    clock = () => performance.now() / 1000, transportOptions = {}}) {
    this.plugin = plugin; this.sampleRate = sampleRate; this.metadata = metadata;
    this.state = 'running';
    this.capabilities = capabilities; this.frame = 0; this.frameAt = clock(); this.outputScope = new Float32Array(2048);
    this.clock = clock; this.epoch = clock(); this.intervalMs = intervalMs; this.autoPoll = autoPoll;
    this.watchIds = []; this.modIds = []; this.values = new Map(); this.page = 0; this.revision = 0;
    this.modToken = 0; this.modBusy = false; this.scopeId = 0; this.scopeBusy = false; this.scopeToken = 0;
    this.disposed = false; this.polling = false;
    this.port = {onmessage:null, postMessage:data => this.message(data), close:() => this.dispose()};
    this.transport = new NativeAudioTransport(plugin, {scheduledMidi:!!capabilities.scheduledMidi,...transportOptions, onError:error => {
      clearTimeout(this.timer); this.state='closed';this.onstatechange?.();
      this.emit({type:'error', message:error.message});this.onprocessorerror?.(error);
    }});
    this.readyTimer = setTimeout(() => {
      if (this.disposed) return;
      this.emit({type:'ready', metadata, sampleRate, oversampling});
      if (autoPoll) this.schedule();
    }, 0);
  }
  get currentTime() { return this.frame / this.sampleRate + (this.state==='running'?Math.max(0, this.clock() - this.frameAt):0); }
  async suspend() {
    if(this.state!=='running')return;
    const snapshot=await this.transport.enqueue(()=>this.plugin.pause());
    if(Number.isSafeInteger(snapshot?.frame))this.frame=snapshot.frame;
    this.state='suspended';this.onstatechange?.();
    clearTimeout(this.timer);
  }
  async resume() {
    if(this.state==='running')return;
    await this.transport.enqueue(()=>this.plugin.resume());
    this.frameAt=this.clock();this.state='running';this.onstatechange?.();
    if(this.autoPoll)this.schedule();
  }
  getFloatTimeDomainData(target) { target.fill(0); target.set(this.outputScope.subarray(0,target.length)); }
  enableScope(enabled) { this.scopeEnabled = !!enabled; }
  emit(data) { if (!this.disposed) this.port.onmessage?.({data}); }
  updateWatch() {
    this.watchIds = [...new Set([...this.modIds, ...(this.scopeId ? [this.scopeId] : [])])];
    this.values.clear(); this.page = 0; ++this.revision;
  }
  message(data) {
    if (this.disposed || this.transport.closed) return;
    switch (data.type) {
      case 'modSubscribe':
        this.modIds = [...new Set((data.sources || []).filter(id => Number.isInteger(id) && id > 0 && id < 128))];
        this.modToken = data.token; this.modBusy = false; this.updateWatch(); return;
      case 'modAck': if (data.token === this.modToken) this.modBusy = false; return;
      case 'scope':
        this.scopeId = Number.isInteger(data.source) && data.source > 0 && data.source < 128 ? data.source : 0;
        this.scopeToken = data.token; this.scopeBusy = false; this.updateWatch(); return;
      case 'scopeAck': if (data.token === this.scopeToken) this.scopeBusy = false; return;
      case 'dispose':
        void this.dispose().catch(error => this.port.onmessage?.({data:{type:'error', message:error.message}})); return;
      default: void this.transport.send(data).catch(() => {});
    }
  }
  schedule() {
    clearTimeout(this.timer);
    if (!this.disposed && !this.transport.closed && this.state==='running') this.timer = setTimeout(async () => {
      try { await this.poll(); } finally { this.schedule(); }
    }, this.intervalMs);
  }
  async poll() {
    if (this.disposed || this.transport.closed || this.polling || this.state!=='running') return;
    this.polling = true;
    const revision = this.revision, offset = this.page * 32, ids = this.watchIds.slice(offset, offset + 32);
    try {
      // Same serial producer as commands: watch IDs cannot change between this watch and its readback.
      const packet = await this.transport.enqueue(async () => {
        await this.plugin.watch({sources:ids}); return this.plugin.read();
      });
      if (this.disposed || this.state!=='running' || revision !== this.revision) return;
      if (packet.pending) return;
      if (packet.disconnected) throw Error('A saída de áudio foi desconectada. Ative o áudio novamente.');
      if (Number.isSafeInteger(packet.frame) && packet.frame >= 0) {this.frame=packet.frame;this.frameAt=this.clock();}
      if (!Array.isArray(packet.cursors) || packet.cursors.length !== 9 ||
          !Array.isArray(packet.sources) || packet.sources.length < ids.length) throw Error('Telemetria nativa inválida');
      const time = this.clock() - this.epoch;
      this.emit({type:'meter', voices:packet.voices, reduction:packet.reduction,
        cursor:packet.cursors[0], cursors:packet.cursors});
      if (this.scopeEnabled && this.capabilities.outputScope) {
        const result = await this.transport.enqueue(() => this.plugin.scope({count:2048}));
        if (result.samples?.length === 2048) this.outputScope.set(result.samples.map(v => Number.isFinite(v) ? v : 0));
      }
      // Legacy read() returns slot values without the IDs actually sampled by the callback. After watch(),
      // those slots may still belong to the previous subscription. Only a coherent ID+value snapshot is usable.
      if (!Array.isArray(packet.sourceIds) || !ids.every((id, i) => packet.sourceIds[i] === id)) return;
      ids.forEach((id, i) => this.values.set(id, Number.isFinite(packet.sources[i]) ? packet.sources[i] : 0));
      if (this.scopeId && ids.includes(this.scopeId) && !this.scopeBusy) {
        this.scopeBusy = true; this.emit({type:'source', token:this.scopeToken, value:this.values.get(this.scopeId), time});
      }
      this.page = offset + 32 >= this.watchIds.length ? 0 : this.page + 1;
      if (this.page === 0 && this.modIds.length && !this.modBusy) {
        this.modBusy = true;
        this.emit({type:'modSources', token:this.modToken, count:this.modIds.length, time,
          values:Float32Array.from(this.modIds.flatMap(id => [id, this.values.get(id) ?? 0]))});
      }
    } catch (error) { this.transport.fail(error); }
    finally { this.polling = false; }
  }
  async report() { return this.transport.enqueue(() => this.plugin.report()); }
  async dispose() {
    if (this.disposePromise) return this.disposePromise;
    this.disposed = true; clearTimeout(this.timer); clearTimeout(this.readyTimer);
    this.disposePromise = this.transport.close().then(() => {
      this.state='closed';this.onstatechange?.();this.port.onmessage?.({data:{type:'disposed'}});
    });
    return this.disposePromise;
  }
}
