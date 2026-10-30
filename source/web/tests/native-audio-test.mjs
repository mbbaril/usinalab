// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {NativeMessageEncoder, NativeOp as O} from '../native-audio-protocol.js';
import {NativeAudioTransport, openAudioBackend} from '../native-audio-transport.js';
import {NativeAudioNode} from '../native-audio-node.js';

const checks = [], check = async (name, fn) => {await fn(); checks.push(name);};
const turn = () => new Promise(r => setImmediate(r));

// Independent receiver follows native_audio.cpp, not the encoder's implementation.
function receive(ops, m) {
  const fixed = {1:['_web_set',2],2:['_web_reset',0],3:['_web_commit',0],4:['_web_midi',3],
    5:['_web_route',4],6:['_web_step',8],7:['_web_step_expression',7],9:['_web_lane',6],
    10:['_web_lane_base',2],11:['_web_globals',2],12:['_web_flush_edits',0],14:['_web_control',2]};
  for (let i=0;i<ops.length;) {
    const code=ops[i++];
    if (fixed[code]) {const [name,n]=fixed[code];m[name](...ops.slice(i,i+n));i+=n;}
    else if (code===8) {
      const [l,s,n]=ops.slice(i,i+3);i+=3;
      new Int32Array(m.HEAPF32.buffer,m._web_chord_buffer(),16).set(ops.slice(i,i+n));i+=n;
      new Float32Array(m.HEAPF32.buffer,m._web_duration_buffer(),16).set(ops.slice(i,i+n));i+=n;
      m._web_chord_durations(l,s,n);
    } else if(code===13) {
      const [index,n]=ops.slice(i,i+2);i+=2;
      new Float32Array(m.HEAPF32.buffer,m._web_function_buffer(),192).set(ops.slice(i,i+3*n));i+=3*n;
      m._web_function(index,n);
    } else if(code===15) m._web_midi(0xb0,123,0);
    else throw Error(`Unknown opcode ${code}`);
  }
}
function recordingModule() {
  const calls=[], m={calls,HEAPF32:new Float32Array(4096),_web_input:c=>c*512,_web_output:c=>1024+c*512,
    _web_chord_buffer:()=>2048,_web_duration_buffer:()=>2112,_web_function_buffer:()=>2304,
    _web_seed:()=>{},_web_init:()=>1,_web_schema:()=>0,UTF8ToString:()=>'{"parameters":[{}]}'};
  for(const name of ['set','reset','commit','midi','route','step','step_expression','lane','lane_base','globals','flush_edits','control'])
    m[`_web_${name}`]=(...args)=>calls.push([name,...args]);
  m._web_chord_durations=(l,s,n)=>calls.push(['chord_durations',l,s,n,
    ...new Int32Array(m.HEAPF32.buffer,2048,n),...new Float32Array(m.HEAPF32.buffer,2112,n)]);
  m._web_function=(i,n)=>calls.push(['function',i,n,...new Float32Array(m.HEAPF32.buffer,2304,n*3)]);
  return m;
}
const body=readFileSync(new URL('../worklet.js',import.meta.url),'utf8').replace(/^import .*;\r?$/gm,'');
const golden=recordingModule();let Processor;
vm.runInNewContext(body,{AudioWorkletProcessor:class{constructor(){this.port={postMessage(){}};}},
  createUsina:async()=>golden,VisualSourcePublisher:class{},sampleRate:48000,currentFrame:0,
  WebAssembly,Float32Array,Int32Array,Float64Array,registerProcessor:(_,p)=>Processor=p});
const processor=new Processor({processorOptions:{seed:1}});await turn();assert(processor.ready);
const clean=value=>JSON.parse(JSON.stringify(value));
const s={on:true,note:60,velocity:100,probability:1,ratchet:2,gate:.5,accent:.75,late:3,condition:2,
  pitchOffset:-2,inherit:false,notes:[60,64,67],durations:{60:2,64:1,67:3}};
const settings={length:16,direction:1,mul:2,div:3,mute:false,mono:false};
const messages=[{type:'params',values:[[1,.25],[2,3]]},{type:'patch',values:[[1,0]]},
  {type:'route',index:2,source:5,destination:30,amount:-.5},
  {type:'step',values:[0,3,1,60,100,1,2,.5],expression:s},
  {type:'lane',values:[2,16,0,1,1,0]},
  {type:'part',lane:0,settings,steps:[s,{...s,on:false,notes:[],durations:undefined}]},
  {type:'part',lane:0,settings:{...settings,mono:true},steps:[s]},
  {type:'sequence',sequence:Array.from({length:9},()=>[s]),lanes:Array.from({length:9},(_,l)=>({...settings,mono:l%2===0}))},
  {type:'function',index:1,points:[[0,0,.5],[.5,1,-.25],[1,0,0]]},
  {type:'control',index:7,value:.75},{type:'globals',resolution:4,swing:.5},{type:'maintenance'},{type:'panic'}];
await check('All edit messages match the real worklet calls, including mono, chords and note durations',()=>{
  const native=recordingModule(),encoder=new NativeMessageEncoder();
  for(const data of messages) {golden.calls.length=0;native.calls.length=0;
    processor.message(data);receive(encoder.encode(data),native);
    assert.deepEqual(clean(native.calls),clean(golden.calls),data.type);
  }
});
await check('MPE note, per-channel bend, pressure, CC74 and Note Off retain ordering and bytes',()=>{
  const encoder=new NativeMessageEncoder(),native=recordingModule();
  const packets=[[0x91,60,100],[0xe1,0,96],[0xd1,90,0],[0xb1,74,110],[0x81,60,0]];
  for(const [status,a,b] of packets) receive(encoder.encode({type:'midi',status,a,b}),native);
  assert.deepEqual(native.calls,packets.map(p=>['midi',...p]));
});
await check('Malformed edits cannot publish partial batches or change MONO state; future MIDI is refused',()=>{
  const e=new NativeMessageEncoder();
  assert.throws(()=>e.encode({type:'part',lane:0,settings:{...settings,mono:true},steps:[{...s,gate:NaN}]}));
  assert.equal(e.mono[0],false);
  for(const bad of [NaN,Infinity,1e100,undefined]) assert.throws(()=>e.encode({type:'control',index:0,value:bad}));
  assert.throws(()=>e.encode({type:'midi',status:0x90,a:60,b:100,frame:48000}));
  assert.throws(()=>e.encode({type:'function',index:0,points:[[0,1],[1,0,0]]}));
});
await check('A full 9×64 sequence with 16-note chords fits in one atomic ring batch',()=>{
  const notes=Array.from({length:16},(_,i)=>48+i), step={...s,notes,durations:{}};
  const ops=new NativeMessageEncoder().encode({type:'sequence',lanes:Array(9).fill(settings),
    sequence:Array.from({length:9},()=>Array(64).fill(step))});
  assert(ops.length<65535);assert.equal(ops.at(-1),O.flush);
  const m=recordingModule();receive(ops,m);assert.equal(m.calls.filter(c=>c[0]==='step').length,576);
});
await check('One producer retries rejected batches before the following Note Off; MIDI fast path is unused',async()=>{
  const seen=[], plugin={async post({ops}){seen.push(ops);return{ok:seen.length>1};},async stop(){}};
  let time=0;const t=new NativeAudioTransport(plugin,{now:()=>time,sleep:async ms=>{time+=ms;}});
  await Promise.all([t.send({type:'midi',status:0x90,a:60,b:100}),t.send({type:'midi',status:0x80,a:60,b:0})]);
  assert.deepEqual(seen.map(a=>a[1]),[0x90,0x90,0x80]);await t.close();
});
await check('Persistent ring refusal closes the host instead of silently dropping Note Off',async()=>{
  let time=0,stops=0;const t=new NativeAudioTransport({post:async()=>({ok:false}),stop:async()=>{stops++;}},
    {now:()=>time,sleep:async ms=>{time+=ms;},timeoutMs:12});
  await assert.rejects(t.send({type:'midi',status:0x80,a:60,b:0}));await t.close();
  assert.equal(stops,1);assert(t.closed);
});
await check('Bounded JS backlog cancels queued notes and waits for an in-flight call before stop',async()=>{
  let release,stops=0;const t=new NativeAudioTransport({post:()=>new Promise(r=>release=r),stop:async()=>{stops++;}}, {maxPending:1});
  const first=t.send({type:'panic'});await turn();
  await assert.rejects(t.send({type:'panic'}));assert.equal(stops,0);
  release({ok:true});await first;await t.close();assert.equal(stops,1);
});
await check('Explicit close discards waiting notes and closes exactly once',async()=>{
  let posts=0,stops=0;const t=new NativeAudioTransport({post:async()=>{posts++;return{ok:true};},stop:async()=>{stops++;}});
  const queued=t.send({type:'midi',status:0x90,a:60,b:100});const end=t.close();
  await assert.rejects(queued);await end;await t.close();assert.equal(posts,0);assert.equal(stops,1);
});
await check('Failed emergency stop stays observable to the backend owner',async()=>{
  const t=new NativeAudioTransport({post:async()=>{throw Error('bridge');},stop:async()=>{throw Error('stop failed');}});
  await assert.rejects(t.send({type:'panic'}),/bridge/);await assert.rejects(t.close(),/stop failed/);
});
await check('Optional input permission failure leaves the output engine playing',async()=>{
  let posts=0;const t=new NativeAudioTransport({post:async()=>{posts++;return{ok:true};},stop:async()=>{}});
  await assert.rejects(t.enqueue(async()=>{throw Error('microphone permission denied');},{fatal:false}));
  assert(!t.closed);await t.send({type:'midi',status:0x90,a:60,b:100});assert.equal(posts,1);await t.close();
});
await check('Web remains default; failed native startup stops before fallback, without changing quality',async()=>{
  const events=[],plugin={start:async({oversampling})=>{events.push(oversampling);return{sampleRate:0};},stop:async()=>events.push('stop')};
  const options={nativePlugin:plugin,oversampling:4,openWebAudio:async()=>{events.push('web');return'wasm';}};
  assert.equal((await openAudioBackend(options)).kind,'web');assert.deepEqual(events,['web']);events.length=0;
  assert.equal((await openAudioBackend({...options,nativeEnabled:true})).kind,'web');assert.deepEqual(events,[4,'stop','web']);
});
await check('Native factory gets metadata; failed stop forbids opening a second engine',async()=>{
  const plugin={start:async()=>({sampleRate:48000,schema:'{"parameters":[{}]}'}),stop:async()=>{throw Error('stop');}};
  let web=0;const options={nativeEnabled:true,nativePlugin:plugin,openWebAudio:async()=>web++,openNative:async info=>info};
  const result=await openAudioBackend(options);assert.equal(result.kind,'native');assert.equal(result.backend.sampleRate,48000);
  await assert.rejects(openAudioBackend({...options,openNative:async()=>{throw Error('init');}}),/stop/);assert.equal(web,0);
});
await check('16x never enters a native host that would silently reduce it to 1x',async()=>{
  let starts=0;const result=await openAudioBackend({nativeEnabled:true,oversampling:16,
    nativePlugin:{start:async()=>starts++},openWebAudio:async()=>({oversampling:16})});
  assert.equal(starts,0);assert.equal(result.kind,'web');assert.equal(result.backend.oversampling,16);
});
await check('Protocol v2 preserves 48-bit scheduled frames and permits explicit 16x support',async()=>{
  const frame=2**40+12345,e=new NativeMessageEncoder({scheduledMidi:true});
  const ops=e.encode({type:'midi',status:0x91,a:60,b:100,frame});
  assert.equal(ops[0],16);assert.equal(ops[1]+ops[2]*2**24,frame);
  let quality=0;
  const result=await openAudioBackend({nativeEnabled:true,oversampling:16,nativePlugin:{
    capabilities:async()=>({maxOversampling:16,scheduledMidi:true}),
    start:async({oversampling})=>{quality=oversampling;return{sampleRate:48000,schema:{parameters:[{}]}};},stop:async()=>{}},
    openNative:async info=>info,openWebAudio:async()=>{throw Error('unexpected fallback');}});
  assert.equal(quality,16);assert.equal(result.kind,'native');assert(result.backend.capabilities.scheduledMidi);
});
await check('Telemetry pages all 127 sources through the 32-slot native window, with tokens and backpressure',async()=>{
  let watched=[],reads=0,stops=0;const packets=[];
  const node=new NativeAudioNode({plugin:{watch:async({sources})=>{watched=sources;assert(sources.length<=32);},
    read:async()=>{reads++;return{voices:2,reduction:0,cursors:Array(9).fill(3),sourceIds:watched,sources:watched.map(id=>id/128)};},
    stop:async()=>stops++,post:async()=>({ok:true})},metadata:{parameters:[{}]},sampleRate:48000,autoPoll:false});
  node.port.onmessage=({data})=>packets.push(data);
  const ids=Array.from({length:127},(_,i)=>i+1);
  node.port.postMessage({type:'modSubscribe',sources:ids,token:9});node.port.postMessage({type:'scope',source:127,token:8});
  for(let i=0;i<8;i++)await node.poll();
  let sources=packets.filter(p=>p.type==='modSources');assert.equal(sources.length,1);assert.equal(sources[0].count,127);
  assert.equal(sources[0].values[253],127/128);assert.equal(packets.filter(p=>p.type==='source').length,1);
  node.port.postMessage({type:'modAck',token:0});for(let i=0;i<4;i++)await node.poll();
  assert.equal(packets.filter(p=>p.type==='modSources').length,1);
  node.port.postMessage({type:'modAck',token:9});node.port.postMessage({type:'scopeAck',token:8});
  for(let i=0;i<4;i++)await node.poll();assert.equal(packets.filter(p=>p.type==='modSources').length,2);
  await node.dispose();const before=reads;await node.poll();assert.equal(reads,before);assert.equal(stops,1);
});
await check('Legacy or stale telemetry cannot relabel values as a different modulation source',async()=>{
  const events=[];let sourceIds;
  const node=new NativeAudioNode({plugin:{watch:async()=>{},read:async()=>({voices:1,reduction:0,
    cursors:Array(9).fill(-1),sourceIds,sources:[.8]}),stop:async()=>{}},
    metadata:{parameters:[{}]},sampleRate:48000,autoPoll:false});
  node.port.onmessage=({data})=>events.push(data);
  node.message({type:'modSubscribe',sources:[5],token:1});await node.poll();
  sourceIds=[6];await node.poll();assert.equal(events.filter(e=>e.type==='modSources').length,0);
  sourceIds=[5];await node.poll();assert.equal(events.filter(e=>e.type==='modSources').length,1);
  await node.dispose();
});
await check('Native pause freezes the sample clock and resume preserves its origin',async()=>{
  let time=0;const events=[];
  const node=new NativeAudioNode({plugin:{pause:async()=>({frame:48000}),resume:async()=>{},stop:async()=>{}},
    metadata:{parameters:[{}]},sampleRate:48000,autoPoll:false,clock:()=>time});
  node.onstatechange=()=>events.push(node.state);
  await node.suspend();time=20;assert.equal(node.currentTime,1);
  await node.resume();time=21;assert.equal(node.currentTime,2);
  await node.dispose();assert.deepEqual(events,['suspended','running','closed']);
});
console.log(JSON.stringify({passed:true,checks},null,2));
