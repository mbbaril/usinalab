// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {algorithmState,envelopeGeometry} from '../synth-visuals.js';
import {bend,evaluate,functionShapes,repeatFunction,shiftFunction} from '../function-editor.js';
const data=JSON.parse(readFileSync(new URL('../algorithms.json',import.meta.url)));
assert.equal(data.algorithms.length,40);
for(let a=0;a<40;a++){
  const zero=algorithmState(data,a,0,()=>0),full=algorithmState(data,a,100,()=>0),half=algorithmState(data,a,50,()=>0);
  assert(zero.carriers>0&&zero.carriers<64);assert.equal(zero.links.length,36);
  for(let i=0;i<36;i++){assert.equal(half.links[i],(zero.links[i]+full.links[i])/2);if(![3,5,33].includes(a))assert.equal(full.links[i],zero.links[i]);}
}
const fifth=algorithmState(data,4,0,()=>0);assert.equal(fifth.carriers,21);
for(const [f,t]of [[1,0],[3,2],[5,4]])assert.equal(fifth.links[f*6+t],1);
const user=algorithmState(data,40,0,id=>id==='direct_out'?42:id==='mx_2to4'?75:0);assert.equal(user.carriers,42);assert.equal(user.links[9],.75);assert.equal(user.links.filter(Boolean).length,1);
const meta=JSON.parse(readFileSync(new URL('../../build-web/dist/metadata.json',import.meta.url))),params=Object.fromEntries(meta.parameters.map(d=>[d.id,d.default]));
Object.assign(params,{op1_atk:30,op1_dec:64,op1_sus:50,op1_rel:85,op1_curve:0});
const linear=envelopeGeometry(id=>params[id],'op1_',meta.envMs);assert.equal(linear.s,.5);assert.equal(linear.xs[4],298);assert(linear.xs.every((x,i)=>!i||x>linear.xs[i-1]));
params.op1_curve=10;const curved=envelopeGeometry(id=>params[id],'op1_',meta.envMs);assert.notEqual(curved.path,linear.path);params.op1_env_mode=1;const adr=envelopeGeometry(id=>params[id],'op1_',meta.envMs);assert.equal(adr.xs[2],adr.xs[3]);
assert.equal(bend(.5,1),1/256);assert.equal(bend(.5,-1),255/256);assert.equal(evaluate(functionShapes[1],.25),.5);assert.equal(evaluate(functionShapes[4],.75),0);
for(const shape of functionShapes){assert(shape.length>=2);assert.equal(shape[0][0],0);assert.equal(shape.at(-1)[0],1);for(const dx of [-1/16,1/16]){const s=shiftFunction(shape,dx);assert(s&&s.length<=64);assert.equal(s[0][0],0);assert.equal(s.at(-1)[0],1);for(let i=1;i<s.length;i++)assert(s[i][0]>=s[i-1][0]);}}
const repeat=repeatFunction(functionShapes[1],3);assert.equal(repeat.length,7);assert.equal(evaluate(repeat,1/6),1);assert.equal(repeatFunction(functionShapes[5],16),null);
console.log('SYNTH parity: 40 native algorithms, feedback-only links, USER direction/outputs, native envelope geometry, function curves/shapes/shift/repeat and point budget: pass');

assert.equal(repeatFunction(functionShapes[1],0),null);assert.equal(repeatFunction(functionShapes[1],-1),null);assert.equal(repeatFunction(functionShapes[1],NaN),null);
