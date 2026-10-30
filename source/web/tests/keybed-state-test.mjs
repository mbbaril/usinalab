// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {KeybedState} from '../keybed-state.js';
const sent=[],k=new KeybedState(p=>sent.push(...p)),pos=(pitch=60,pressure=.5)=>({pitch,pressure,x:20,y:40});
k.begin(1,60,pos());k.begin(2,60,pos());assert.equal(sent.filter(m=>m[0]===0x90).length,1);k.end(1);assert.equal(sent.filter(m=>m[0]===0x80).length,0);k.press('A',60);k.end(2);assert.equal(sent.filter(m=>m[0]===0x80).length,0);k.release('A');assert.deepEqual(sent.at(-1),[0x80,60,0]);
k.begin(1,60,pos());k.move(1,{note:62});assert.deepEqual(sent.slice(-2),[[0x80,60,0],[0x90,62,100]]);k.move(1,{note:null});assert.deepEqual(sent.at(-1),[0x80,62,0]);k.end(1);
k.configure(true,12);sent.length=0;k.begin(1,60,pos());k.begin(2,60,pos());assert.deepEqual(sent.filter(m=>(m[0]&0xf0)===0x90).map(m=>m[0]),[0x91,0x92]);assert(sent.some(m=>m[0]===0xb1&&m[1]===6&&m[2]===12));
k.move(1,{...pos(63,1),x:80});assert.equal(k.encoder.voices.get(1).pitch,10240);assert.equal(k.encoder.voices.get(1).pressure,127);assert.equal(k.encoder.voices.get(2).pitch,8192);
k.setTimbre(.2);assert.equal(k.encoder.voices.get(1).timbre,25);assert.equal(k.encoder.voices.get(2).timbre,25);k.end(1);assert.equal(k.encoder.voices.size,1);
sent.length=0;k.configure(true,24);assert.equal(sent.filter(m=>(m[0]&0xf0)===0x80).length,0);assert.equal(k.encoder.voices.size,1);
k.press('A',60);k.release('A');assert.deepEqual(sent.slice(-2),[[0x90,60,100],[0x80,60,0]]);assert.equal(k.encoder.voices.size,1);
for(let i=3;i<=18;i++)k.begin(i,60,pos());assert.equal(k.encoder.voices.size,15);assert.equal(k.fingers.size,15);assert.equal(k.encoder.voices.has(2),false);k.end(2);assert.equal(k.encoder.voices.size,15);
k.stop();assert.equal(k.fingers.size,0);assert.equal(k.encoder.voices.size,0);assert.equal(k.keys.size,0);const count=sent.length;k.stop();assert.equal(sent.length,count);
k.begin(1,60,pos());sent.length=0;k.stop(false);assert.equal(sent.length,0);assert.equal(k.encoder.voices.size,0);
console.log('Local keybed: gliss ownership, duplicate notes, per-finger MPE, RPN, pressure/timbre, live range, channel steal and cancel/panic: pass');
