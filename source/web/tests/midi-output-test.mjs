// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {MidiOutput} from '../midi-output.js';
globalThis.window={Capacitor:{isNativePlatform:()=>false}};
const bytes=[],port={id:'test',name:'Test',state:'connected',async open(){},async close(){},send(b){bytes.push([...b]);}};
Object.defineProperty(globalThis,'navigator',{configurable:true,value:{requestMIDIAccess:async()=>({outputs:new Map([['test',port]])})}});
const out=new MidiOutput();await out.connect();await out.select('test');
await Promise.all([out.send([[145,60,100]]),out.send([[209,64,0],[225,127,127]])]);
assert.deepEqual(bytes.flat(),[145,60,100,209,64,225,127,127]);
await out.panic();assert(bytes.flat().some((b,i,a)=>b===129&&a[i+1]===60));assert(out.ready);
bytes.length=0;const pending=out.send([[146,70,100]]);const released=out.panic();await assert.rejects(pending,e=>e.canceled);await released;assert(!bytes.flat().includes(146));
await out.send([[145,64,100]]);bytes.length=0;out.fail(Error('disconnect'));assert(!out.ready);assert.deepEqual(bytes[0].slice(0,3),[129,64,0]);
// Native queue remains ordered across asynchronous bridge calls, and panic follows the active send.
const order=[],native=new MidiOutput();let finish;
native.native={sendOutput:async({messages})=>{order.push(messages);await new Promise(r=>finish=r);},panicOutput:async()=>order.push('panic')};native.ready=true;
const a=native.send([[145,60,100]]);await new Promise(r=>setTimeout(r,0));const b=native.send([[146,62,100]]);const bRejected=assert.rejects(b,e=>e.canceled);const panic=native.panic();finish();await a;await bRejected;await panic;
assert.equal(order.length,2);assert.equal(order[1],'panic');
console.log('MIDI output: ordered batches, channel pressure framing, owned note release, cancellation, bridge sequencing and disconnect cleanup passed.');
