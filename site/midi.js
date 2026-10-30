// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Mario Brandalise Baril
import { MidiParser } from './midi-parser.js';
export class MidiConnection {
  constructor(onMessage, onStatus, onDisconnect) {
    this.onMessage = onMessage; this.onStatus = onStatus; this.onDisconnect = onDisconnect;
    this.parsers = new Map(); this.connected = false;
  }
  parser(id) { if (!this.parsers.has(id)) this.parsers.set(id, new MidiParser(this.onMessage)); return this.parsers.get(id); }
  async connect() {
    if (window.Capacitor?.isNativePlatform()) {
      // The native bridge bundle is supplied by the mobile build, never required for a browser-only visit.
      const { registerPlugin } = await import('./capacitor-core.js');
      this.native = registerPlugin('UsinaMidi');
      if (!this.connected) {
        await this.native.addListener('midi', e => this.parser(e.id || 'native').push(e.bytes, performance.now()));
        await this.native.addListener('devices', e => { this.onStatus(e.message || 'Dispositivos MIDI atualizados.'); this.onDisconnect(); });
        this.connected = true;
      }
      await this.native.connect();
      const result = await this.native.devices();
      this.onStatus(result.devices?.length ? `MIDI: ${result.devices.map(d => d.name).join(', ')}` : 'MIDI ativo. Conecte seu controlador USB/OTG ou escolha BLE.');
      return;
    }
    if (!navigator.requestMIDIAccess) throw Error('Este navegador não oferece MIDI USB/BLE. No iPad/iPhone, use o teclado de toque nesta versão; no Mac, um navegador com Web MIDI.');
    if (!this.access) {
      this.access = await navigator.requestMIDIAccess({ sysex: false });
      this.access.onstatechange = () => { this.attach(); this.onDisconnect(); };
    }
    this.attach();
  }
  attach() {
    const names = [];
    for (const port of this.access.inputs.values()) {
      if (port.state !== 'connected') continue; names.push(port.name);
      port.onmidimessage = e => this.parser(port.id).push(e.data, e.receivedTime || e.timeStamp || performance.now());
    }
    this.onStatus(names.length ? `MIDI: ${names.join(', ')}` : 'MIDI autorizado. Conecte USB/OTG; BLE deve estar conectado como dispositivo MIDI no sistema.');
  }
  async bluetooth() {
    if (!this.native) await this.connect();
    if (this.native) { this.onStatus('Buscando instrumentos MIDI BLE…'); return this.native.bluetooth(); }
    throw Error('Para conectar MIDI BLE pelo sistema, use o app Usina. No Mac, conecte primeiro em Configuração de Áudio e MIDI.');
  }
}
