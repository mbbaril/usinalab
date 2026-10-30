// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Mario Brandalise Baril
import {NativeMessageEncoder} from './native-audio-protocol.js';

/** One producer for every JNI command, with bounded backlog and ordered retry on ring refusal. */
export class NativeAudioTransport {
  constructor(plugin, {onError = () => {}, maxPending = 128, retryMs = 4, timeoutMs = 100,
    scheduledMidi = false, now = () => performance.now(), sleep = ms => new Promise(r => setTimeout(r, ms))} = {}) {
    this.plugin = plugin; this.encoder = new NativeMessageEncoder({scheduledMidi}); this.onError = onError;
    this.maxPending = maxPending; this.retryMs = retryMs; this.timeoutMs = timeoutMs;
    this.now = now; this.sleep = sleep; this.pending = 0; this.closed = false; this.tail = Promise.resolve();
  }
  fail(error) {
    if (this.closed) return;
    this.closed = true;
    this.error = error;
    // The serialized chain waits for the in-flight JNI call before freeing its host.
    this.stopping = this.tail.catch(() => {}).then(() => this.plugin.stop()).catch(error => { this.stopError = error; });
    this.onError(error);
  }
  enqueue(task, {fatal = true} = {}) {
    if (this.closed) return Promise.reject(this.error || Error('Áudio nativo encerrado'));
    if (this.pending >= this.maxPending) {
      const error = Error('Fila de áudio cheia. O motor foi encerrado para liberar as notas.');
      this.fail(error); return Promise.reject(error);
    }
    ++this.pending;
    const result = this.tail.then(async () => {
      if (this.closed) throw this.error || Error('Áudio nativo encerrado');
      return task();
    });
    this.tail = result.catch(error => {if(fatal)this.fail(error);}).finally(() => { --this.pending; });
    return result;
  }
  send(data) {
    let ops;
    try { ops = this.encoder.encode(data); }
    catch (error) { this.fail(error); return Promise.reject(error); }
    return this.enqueue(async () => {
      const deadline = this.now() + this.timeoutMs;
      for (;;) {
        if (this.closed) throw this.error || Error('Áudio nativo encerrado');
        const result = await this.plugin.post({ops});
        if (result?.ok === true) return;
        if (this.now() >= deadline) throw Error('O motor nativo não aceitou as mensagens. Áudio encerrado.');
        await this.sleep(this.retryMs);
      }
    });
  }
  async drain() { await this.tail; if (this.error) throw this.error; }
  async close() {
    if (!this.closed) {
      this.closed = true;
      this.stopping = this.tail.catch(() => {}).then(() => this.plugin.stop());
    }
    await this.stopping;
    if (this.stopError) throw this.stopError;
  }
}

/** Startup-only fallback. A playing native engine must be stopped before Web Audio is opened. */
export async function openAudioBackend({nativePlugin, nativeEnabled = false, oversampling = 4, openWebAudio, openNative}) {
  if (!nativeEnabled || !nativePlugin) return {kind:'web', backend:await openWebAudio()};
  try {
    const capabilities = typeof nativePlugin.capabilities === 'function' ? await nativePlugin.capabilities() : {};
    if (![1,2,4,8,...(capabilities.maxOversampling >= 16 ? [16] : [])].includes(oversampling)) {
      return {kind:'web', backend:await openWebAudio(), nativeError:Error('Esta qualidade exige outro backend.')};
    }
    const result = await nativePlugin.start({oversampling});
    if (!(result.sampleRate > 0)) throw Error('Saída nativa indisponível');
    const metadata = typeof result.schema === 'string' ? JSON.parse(result.schema) : result.schema;
    if (!Array.isArray(metadata?.parameters) || !metadata.parameters.length) throw Error('Schema nativo inválido');
    return {kind:'native', backend:await openNative({plugin:nativePlugin, metadata, sampleRate:result.sampleRate, capabilities})};
  } catch (nativeError) {
    // If stop fails, do not risk two engines sounding simultaneously.
    await nativePlugin.stop();
    return {kind:'web', backend:await openWebAudio(), nativeError};
  }
}
