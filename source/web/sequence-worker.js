// SPDX-License-Identifier: GPL-3.0-only
import create from './usina.mjs';
import {encodeSequence,decodeLane,wireSize,reconcileChords} from './sequence-wire.js';
// No Instrument/audio initialization: edit commands use the pure C++ pattern helper in a dedicated worker.
let runtime;
self.onmessage=async({data})=>{try{
  runtime??=create();const m=await runtime,wire=new Float32Array(m.HEAPF32.buffer,m._web_sequence_buffer(),wireSize);encodeSequence(data.state,wire);
  if(!m._web_sequence_command(data.lane,data.operation,data.pulses,data.rotation,data.amount/100,data.seed))throw Error('Não foi possível gerar o padrão.');
  const settings=data.state.lanes[data.lane],scales=JSON.parse(m.UTF8ToString(m._web_schema())).sequenceScales;const before=data.state.sequence[data.lane],steps=reconcileChords(before,decodeLane(wire,data.lane,before),data.operation,data.policy,settings.length,scales?.[settings.scale],settings.root??0);self.postMessage({id:data.id,lane:data.lane,steps});
}catch(error){runtime=null;self.postMessage({id:data.id,error:String(error.message||error)});}};
