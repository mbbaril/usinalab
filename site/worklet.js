// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Mario Baril
import {VisualSourcePublisher} from './visual-sources.js';
import createUsina from './usina.mjs';

class UsinaProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.ready = false;
    this.modVisuals=new VisualSourcePublisher(packet=>this.port.postMessage(packet),id=>this.m._web_source(id));
    this.capacity = 2048;
    this.frames = new Float64Array(this.capacity);
    this.status = new Int32Array(this.capacity);
    this.a = new Int32Array(this.capacity);
    this.b = new Int32Array(this.capacity);
    this.count = 0;
    this.ticks = 0;
    this.cursors = new Int32Array(9);
    this.failed = false;
    this.disposed = false;
    this.scopeSource=0;this.scopeToken=0;this.scopeFrames=0;this.scopeBusy=false;this.laneMono=Array(9).fill(false);
    this.port.onmessage = e => this.message(e.data);
    const { module, oversampling = 4, seed = 1 } = options.processorOptions;
    createUsina({
      instantiateWasm(imports, done) {
        const instance = new WebAssembly.Instance(module, imports);
        done(instance, module);
        return instance.exports;
      },
      printErr: text => this.port.postMessage({ type: 'error', message: String(text) }),
    }).then(m => {
      this.m = m;
      m._web_seed(seed);
      if (!m._web_init(sampleRate, oversampling)) throw Error('Taxa de audio invalida');
      this.input = [0, 1].map(c => new Float32Array(m.HEAPF32.buffer, m._web_input(c), 128));
      this.output = [0, 1].map(c => new Float32Array(m.HEAPF32.buffer, m._web_output(c), 128));
      this.functionBuffer = new Float32Array(m.HEAPF32.buffer, m._web_function_buffer(), 64 * 3);
      this.chordBuffer = new Int32Array(m.HEAPF32.buffer, m._web_chord_buffer(), 16);
      this.durationBuffer = new Float32Array(m.HEAPF32.buffer,m._web_duration_buffer(),16);
      this.ready = true;
      this.port.postMessage({ type: 'ready', metadata: JSON.parse(m.UTF8ToString(m._web_schema())), sampleRate, oversampling });
    }).catch(error => { this.failed = true; this.port.postMessage({ type: 'error', message: String(error) }); });
  }
  message(data) {
    if (!this.ready) return;
    const m = this.m;
    try {
      switch (data.type) {
        case 'modSubscribe': this.modVisuals.configure(data.sources,data.token);break;
        case 'modAck': this.modVisuals.ack(data.token);break;
        case 'scope': this.scopeSource=Number.isInteger(data.source)&&data.source>0&&data.source<128?data.source:0;this.scopeToken=data.token;this.scopeFrames=0;this.scopeBusy=false;break;
        case 'scopeAck': if(data.token===this.scopeToken)this.scopeBusy=false;break;
        case 'params':
          for (const [index, value] of data.values) m._web_set(index, value);
          m._web_commit();
          break;
        case 'patch':
          m._web_reset();
          for (const [index, value] of data.values) m._web_set(index, value);
          m._web_commit();
          break;
        case 'midi': {
          if (this.count === this.capacity) { this.panic(); break; }
          const frame = Math.max(currentFrame, Number.isFinite(data.frame) ? data.frame : currentFrame);
          let i = this.count++;
          while (i > 0 && this.frames[i - 1] > frame) {
            this.frames[i] = this.frames[i - 1]; this.status[i] = this.status[i - 1]; this.a[i] = this.a[i - 1]; this.b[i] = this.b[i - 1]; --i;
          }
          this.frames[i] = frame; this.status[i] = data.status; this.a[i] = data.a; this.b[i] = data.b;
          break;
        }
        case 'route': m._web_route(data.index, data.source, data.destination, data.amount); break;
        case 'step': m._web_step(...data.values);this.setExpression(data.values[0],data.values[1],data.expression); this.setChord(data.values[0],data.values[1],data.expression??{notes:data.notes});m._web_flush_edits(); break;
        case 'lane': m._web_lane(...data.values); m._web_flush_edits(); break;
        case 'part': {
          // One existing part is edited in place; no scheduler replacement or transport reset.
          const l=data.lane,s=data.settings;this.laneMono[l]=!!s.mono;m._web_lane(l,s.length,s.direction,s.mul,s.div,+s.mute);
          m._web_lane_base(l,s.baseNote??(l>=5?[36,41,48,55][l-5]:60));
          data.steps.forEach((step,i)=>this.setStep(l,i,step));
          m._web_flush_edits();break;
        }
        case 'sequence':
          for (let l = 0; l < data.sequence.length; ++l) {
            const lane = data.lanes[l];this.laneMono[l]=!!lane.mono;
            m._web_lane(l, lane.length, lane.direction, lane.mul, lane.div, +lane.mute);
            m._web_lane_base(l,lane.baseNote??(l>=5?[36,41,48,55][l-5]:60));
            data.sequence[l].forEach((s, i) => this.setStep(l,i,s));
          }
          m._web_flush_edits();
          break;
        case 'control': m._web_control(data.index, data.value); break;
        case 'function':
          if (Array.isArray(data.points) && data.points.length >= 2 && data.points.length <= 64) {
            for (let i = 0; i < data.points.length; ++i) for (let j = 0; j < 3; ++j) this.functionBuffer[3 * i + j] = data.points[i][j];
            m._web_function(data.index, data.points.length);
          }
          break;
        case 'maintenance': m._web_flush_edits(); break;
        case 'globals': m._web_globals(data.resolution, data.swing); break;
        case 'panic': this.panic(); break;
        case 'dispose': this.panic(); m._web_dispose(); this.disposed = true; this.ready = false; this.port.postMessage({ type: 'disposed' }); break;
      }
    } catch (error) { this.panic(); this.port.postMessage({ type: 'error', message: String(error) }); }
  }
  setChord(l,i,s){const notes=Array.isArray(s.notes)?s.notes:s.durations?[s.note]:null,mono=this.laneMono[l],root=s.note;const list=mono?(s.durations?.[root]?[root]:[]):notes||[],count=Math.min(16,list.length);for(let k=0;k<count;k++){this.chordBuffer[k]=Math.round(list[k]);this.durationBuffer[k]=s.durations?.[list[k]]??0;}this.m._web_chord_durations(l,i,count);}

  setExpression(l,i,s={}){this.m._web_step_expression(l,i,s.accent??0,s.late??0,s.condition??0,s.pitchOffset??0,+!!s.inherit);}
  setStep(l,i,s){this.m._web_step(l,i,+s.on,s.note,s.velocity,s.probability,s.ratchet,s.gate);this.setExpression(l,i,s);this.setChord(l,i,s);}
  panic() { this.count = 0; if (this.m) this.m._web_midi(0xb0, 123, 0); }
  process(inputs, outputs) {
    const out = outputs[0];
    if (this.disposed) return false;
    if (!this.ready || this.failed) return true;
    const n = out[0].length;
    const input = inputs[0];
    let at = 0, consumed = 0;
    while (at < n) {
      while (consumed < this.count && this.frames[consumed] <= currentFrame + at) {
        this.m._web_midi(this.status[consumed], this.a[consumed], this.b[consumed]); ++consumed;
      }
      const next = consumed < this.count ? Math.min(n, Math.max(at + 1, Math.ceil(this.frames[consumed] - currentFrame))) : n;
      const length = Math.min(128, next - at);
      for (let c = 0; c < 2; ++c) {
        const source = input[c] || input[0];
        for (let i = 0; i < length; ++i) this.input[c][i] = source ? source[at + i] : 0;
      }
      this.m._web_render(length);
      for (let c = 0; c < out.length; ++c) for (let i = 0; i < length; ++i) out[c][at + i] = this.output[c][i];
      at += length;
    }
    if (consumed) {
      this.count -= consumed;
      for (let i = 0; i < this.count; ++i) {
        this.frames[i] = this.frames[i + consumed]; this.status[i] = this.status[i + consumed]; this.a[i] = this.a[i + consumed]; this.b[i] = this.b[i + consumed];
      }
    }
    this.modVisuals.tick(n,(currentFrame+n)/sampleRate,sampleRate);
    if(this.scopeSource){this.scopeFrames+=n;if(this.scopeFrames>=sampleRate/30){this.scopeFrames%=sampleRate/30;if(!this.scopeBusy){this.scopeBusy=true;this.port.postMessage({type:'source',token:this.scopeToken,value:this.m._web_source(this.scopeSource),time:(currentFrame+n)/sampleRate});}}}
    if (++this.ticks % 32 === 0) {for(let i=0;i<9;++i)this.cursors[i]=this.m._web_cursor(i);this.port.postMessage({ type: 'meter', voices: this.m._web_meter(0), reduction: this.m._web_meter(1), cursor: this.cursors[0], cursors:this.cursors });}
    return true;
  }
}
registerProcessor('usina', UsinaProcessor);
