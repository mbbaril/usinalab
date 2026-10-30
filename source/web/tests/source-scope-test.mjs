import assert from 'node:assert/strict';
import {SourceHistory,sourceBipolar} from '../source-scope.js';
const history=new SourceHistory(150);for(let i=0;i<400;i++)assert(history.push(Math.sin(i),i/30));assert.equal(history.count,150);assert.equal(history.points()[0][0],250/30);assert.equal(history.points().at(-1)[0],399/30);assert(!history.push(0,0));assert(!history.push(NaN,14));assert(!history.push(0,Infinity));history.reset();assert(history.push(0,0));assert.equal(history.count,1);
for(const id of ['lfo1','rand1','comb1','macro1'])assert(sourceBipolar(id));for(const id of ['env1','func1','ctrl1'])assert(!sourceBipolar(id));
console.log('Source history: bounded ring, time order, invalid samples, source reset and native display domains pass');
