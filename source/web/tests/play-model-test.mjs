// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {whitesFor,organKeys,organNoteAt,gridPitch,velocityAt,mixSnapshots,loopAt,closeLoop,macroCc,parsePlayState,defaultPlayState,noteName,maxManuals} from '../play-model.js';
// the organ: 3 octaves and the top C, 22 whites and 15 blacks; C2 = 48 at the left, the top C 84
const keys=organKeys(2);assert.equal(keys.filter(k=>!k.black).length,22);assert.equal(keys.filter(k=>k.black).length,15);
assert.equal(keys[0].note,48);assert.equal(Math.max(...keys.map(k=>k.note)),84);assert.equal(noteName(48),'C2');assert.equal(noteName(60),'C3');
assert.deepEqual(organNoteAt(2,.01,.9),{note:48,rel:.9});                 // the first white, low on it
assert.equal(organNoteAt(2,1/22,.3).note,49);                               // between C and D, high: C#
assert.equal(organNoteAt(2,1/22,.8).note,50);                               // the same x, low: the white under it, D
assert.equal(organNoteAt(2,2.99/22,.3).note,52);                            // no black after E
assert.equal(organNoteAt(2,1.2,.5),null);
// the phone's manual: 10 whites (C2..E3), its blacks among them; the tablet 15
assert.equal(whitesFor(390),10);assert.equal(whitesFor(800),15);assert.equal(whitesFor(1280),22);
assert.equal(organKeys(2,10).filter(k=>!k.black).length,10);assert.equal(Math.max(...organKeys(2,10).map(k=>k.note)),64);assert.equal(organNoteAt(2,.95,.9,10).note,64);assert.ok(organKeys(8).every(k=>k.note<128));
// the grid: the CABINE's rule (minor, A: the bottom left the root, a row a fourth = three degrees up)
assert.equal(gridPitch(5,0,{scale:1,root:9,octave:2}),57);assert.equal(gridPitch(4,0,{scale:1,root:9,octave:2}),62);
assert.equal(gridPitch(5,7,{scale:1,root:9,octave:2}),69);assert.equal(gridPitch(6,0),-1);
for(let scale=0;scale<8;++scale)for(let row=0;row<6;++row)for(let col=0;col<16;++col){const n=gridPitch(row,col,{scale,root:11,octave:8});assert.ok(n===-1||n>=0&&n<128);}
// the velocity: the key's top soft, its bottom hard; off, 100
assert.equal(velocityAt(0,true),20);assert.equal(velocityAt(1,true),127);assert.equal(velocityAt(.2,false),100);
// the snapshots: A +0.5 / D -0.5 on MACRO 1; the centre 0, A's corner A, an empty corner 0
const snaps=[[.5,0,0,0],[0,0,0,0],[0,0,0,0],[-.5,1,0,0]];
assert.equal(mixSnapshots(snaps,0b1001,.5,.5)[0],0);assert.equal(mixSnapshots(snaps,0b1001,0,0)[0],.5);assert.equal(mixSnapshots(snaps,0b1001,1,0)[0],0);
assert.equal(mixSnapshots(snaps,0b1001,1,1)[1],1);assert.equal(mixSnapshots(snaps,0,1,1)[1],0);
// the loop: closed back to its start, read across its end
const loop=closeLoop([{t:0,x:0,y:0},{t:.5,x:1,y:0},{t:1,x:1,y:1}]);assert.equal(loop.length,4);
assert.deepEqual(loopAt(loop,.25),{x:.5,y:0});assert.deepEqual(loopAt(loop,loop.at(-1).t+.25),{x:.5,y:0});assert.equal(closeLoop([{t:0,x:0,y:0},{t:.1,x:1,y:1},{t:.2,x:0,y:1}]),null);
// the macros as CCs, the state read back safely, the phone's manuals
assert.equal(macroCc(-1),0);assert.equal(macroCc(0),64);assert.equal(macroCc(1),127);
assert.deepEqual(parsePlayState(null),defaultPlayState());
const bad=parsePlayState({snaps:[[9,-9]],set:99,x:7,loop:[{t:'a'},{t:1,x:3,y:-2}],layout:'x',manuals:9,octaves:[-3,12],glide:44,dest:'control',scale:99});
assert.equal(bad.snaps[0][0],1);assert.equal(bad.snaps[0][1],-1);assert.equal(bad.set,15);assert.equal(bad.x,1);assert.deepEqual(bad.loop,[{t:1,x:1,y:0}]);
assert.equal(bad.layout,'organ');assert.equal(bad.manuals,3);assert.deepEqual(bad.octaves,[0,8,4]);assert.equal(bad.glide,5);assert.equal(bad.dest,'control');assert.equal(bad.scale,7);
assert.equal(parsePlayState({slide:'bend'}).slide,'bend');assert.equal(parsePlayState({slide:'x'}).slide,null);
assert.equal(maxManuals(390),2);assert.equal(maxManuals(1280),3);
console.log('PLAY model: organ keys, grid, velocity, snapshot mix, loop, macro CCs and saved state passed.');
