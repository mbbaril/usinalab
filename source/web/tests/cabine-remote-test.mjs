// SPDX-License-Identifier: GPL-3.0-only
// Replay the shared remote protocol fixture. Check outgoing gesture bytes and
// incoming LED colors and active surfaces against the native conversation.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {remote,RemoteGrid,placeOf,SURFACES,TOP,SIDE} from '../cabine-remote.js';
const fixture=JSON.parse(readFileSync(new URL('./fixtures/cabine-remote.json',import.meta.url),'utf8'));
// the gestures behind each step, as the phone's grid makes them
const gesture={
  'hello: all the colours':()=>[remote.hello()],
  'surface DRUMS':()=>[remote.surface(SURFACES.indexOf('DRUMS'))],
  'drum 1, step 1 tapped (OFF -> WEAK)':()=>[remote.pad(0,0,true),remote.pad(0,0,false)],
  'surface KEYS':()=>[remote.surface(SURFACES.indexOf('KEYS'))],
  'the bottom left key held':()=>[remote.pad(7,0,true)],
  'and let go':()=>[remote.pad(7,0,false)],
  'surface ROTAS':()=>[remote.surface(SURFACES.indexOf('ROTAS'))],
};
const grid=new RemoteGrid();let steps=0;
for(const step of fixture.steps){
  if(gesture[step.name])assert.deepEqual(gesture[step.name](),step.in,`the phone's bytes for "${step.name}"`);
  for(const[ch,cc,v]of step.out)assert.ok(grid.receive(0xb0|(ch-1),cc,v),`"${step.name}": ${ch}/${cc} is the protocol's`);
  for(const[place,rgb]of Object.entries(step.grid))assert.deepEqual(grid.colour(+place),rgb,`"${step.name}": place ${place}`);
  assert.equal(grid.surface,step.surface,`"${step.name}": the surface`);++steps;
}
assert.ok(steps>=8&&fixture.steps.some(s=>s.name.startsWith('another')&&s.out.length===0));
// the places: the pads 11..88, the top row 91..98, the right column 19..89 - never a CC the protocol uses for itself
assert.equal(placeOf(0,0),81);assert.equal(placeOf(7,7),18);assert.ok([...TOP,...SIDE].every(cc=>cc<100&&cc>10));
assert.equal(new RemoteGrid().receive(0x90,60,100),false);assert.equal(new RemoteGrid().receive(0xb0,1,64),false);   // not its: the app keeps them
console.log(`CABINE REMOTA contract: ${steps} steps of the plugin's conversation replayed - the phone's bytes, every pad's colour and the surface agree.`);
