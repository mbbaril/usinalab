import assert from 'node:assert/strict';
import {createSequenceGenerator} from '../sequence-generator.js';
let worker;globalThis.Worker=class{constructor(){worker=this;}postMessage(value){this.request=structuredClone(value);}terminate(){}};
const steps=Array.from({length:64},()=>({on:false,note:60})),state={lanes:[{length:16}],sequence:[steps]},sent=[],messages=[];const generator=createSequenceGenerator({state,send:v=>sent.push(v),message:v=>messages.push(v),changed:()=>{}});
generator.run(0,0,{pulses:4});assert(generator.busy);state.sequence[0][0].on=true;worker.onmessage({data:{id:worker.request.id,lane:0,steps}});assert.equal(sent.length,0);assert(!generator.busy);assert(messages[0].includes('mudou'));
generator.run(0,0,{});const result=structuredClone(steps);result[1].on=true;worker.onmessage({data:{id:worker.request.id,lane:0,steps:result}});assert.equal(sent.length,1);assert.equal(sent[0].type,'part');assert.equal(state.sequence[0][1].on,true);assert(!generator.busy);
generator.run(0,0,{});worker.onmessage({data:{id:worker.request.id,error:'teste'}});assert(!generator.busy);assert.equal(sent.length,1);console.log('Generator transaction: stale edits rejected, atomic part commit, busy state and failure recovery pass');

// Importing an identical session invalidates the old response by identity, not content.
generator.run(0,0,{});state.sequence=structuredClone(state.sequence);const sends=sent.length;worker.onmessage({data:{id:worker.request.id,steps:result}});assert.equal(sent.length,sends);assert(!generator.busy);
