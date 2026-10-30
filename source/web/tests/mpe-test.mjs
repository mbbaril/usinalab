// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {MpeEncoder,pitchCoordinate} from '../mpe.js';
const sent=[];const mpe=new MpeEncoder(p=>sent.push(...p));
mpe.setup();assert.equal(sent.length,101);assert.deepEqual(sent.slice(0,5),[[176,101,0],[176,100,6],[176,6,15],[176,101,127],[176,100,127]]);
assert(sent.some(m=>m[0]===191&&m[1]===6&&m[2]===48));sent.length=0;
assert.equal(mpe.begin('a',60,100,.25),2);assert.equal(mpe.begin('b',60,100,.75),3);
assert.equal(mpe.voices.size,2);sent.length=0;
mpe.expression('a',{semitones:48,pressure:1,timbre:0});
assert.deepEqual(sent,[[225,127,127],[209,127,0],[177,74,0]]);sent.length=0;
mpe.expression('a',{semitones:48,pressure:1,timbre:0});assert.equal(sent.length,0);
mpe.expression('b',{semitones:-100});assert.deepEqual(sent.pop(),[226,0,0]);
mpe.end('a');assert.equal(mpe.voices.size,1);assert.equal(mpe.voices.get('b').note,60);assert(sent.some(m=>m[0]===129&&m[1]===60));
const before=sent.length;mpe.end('a');assert.equal(sent.length,before);
for(let i=0;i<15;++i)mpe.begin(i,60+i);assert.equal(mpe.voices.size,15);assert(!mpe.voices.has('b'));assert(sent.some(m=>m[0]===130&&m[1]===60));
assert.equal(new Set([...mpe.voices.values()].map(v=>v.channel)).size,15);
mpe.clear();assert.equal(mpe.voices.size,0);
const centres=[{x:0,note:60},{x:20,note:61},{x:50,note:62}];assert.equal(pitchCoordinate(35,centres),61.5);assert.equal(pitchCoordinate(-20,centres),59);
console.log('MPE: setup RPN, independent equal notes, 14-bit bend, pressure, timbre, channel steal and key interpolation passed.');
