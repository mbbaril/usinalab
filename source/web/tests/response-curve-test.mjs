// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {responsePath,createResponseCurve} from '../response-curve.js';
let time=0,callback,id=0,path='';const canceled=[];const a=createResponseCurve({write:p=>path=p,clock:()=>time,request:f=>{callback=f;return ++id;},cancel:i=>canceled.push(i)});
a.set([0,0,0]);const start=path;a.set([-24,-24,-24]);assert.equal(path,start);time=50;callback();assert.equal(path,responsePath([-12,-12,-12]));time=100;callback();assert.equal(path,responsePath([-24,-24,-24]));
a.set([24,24,24]);time=125;callback();const midway=path;a.set([0,0,0]);assert.equal(path,midway,'new response starts at current visible curve');time=225;callback();assert.equal(path,responsePath([0,0,0]));
assert.equal(responsePath([0,NaN,0]).match(/M/g).length,2,'do not connect across unavailable frequency bins');a.clear('M0 68H360');assert.equal(path,'M0 68H360');a.set([0,0,0]);assert.equal(path,start);a.stop();
console.log('Response animation: continuous measured endpoints, interrupted interpolation, finite-bin gaps and cancellation passed.');
