// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {createUserPerformance,userPadAt} from '../cabine-user.js';
import {createCabineSequence,drumLevel,installDrumTouch} from '../cabine-sequence.js';
import {cabineLayout} from '../cabine-keys.js';
const params={macro1:.3,cutoff:.6,fx_a_mix:.2,fx_b_mix:.7,seq_bpm:120,lfo1_sync:0,dly_sync:0},list=[{name:'INIT'},{name:'A'},{name:'B'}],state={preset:'A'},chosen=[];
const u=createUserPerformance({normalized:id=>params[id]??0,setNormalized:(id,v)=>params[id]=v,param:id=>params[id]??0,setParam:(id,v)=>params[id]=v,library:()=>list,choosePreset:p=>{state.preset=p.name;chosen.push(p.name);},state});
u.press(16);assert.equal(params.macro1,.125);u.press(23);assert.equal(params.macro1,1);u.release(16);assert.equal(params.macro1,1);u.release(23);assert.equal(params.macro1,.3);
u.press(23);u.press(16);u.release(16);assert.equal(params.macro1,1);u.cancel();assert.equal(params.macro1,.3);
u.press(48);assert.equal(params.fx_a_mix,0);assert.equal(params.fx_b_mix,0);u.press(49);u.release(48);assert.equal(params.fx_a_mix,1);u.cancel();assert.equal(params.fx_a_mix,.2);assert.equal(params.fx_b_mix,.7);
params.dly_freeze=1;u.press(8);u.release(8);assert.equal(params.dly_freeze,0);params.tex_freeze=1;u.press(10);u.release(10);assert.equal(params.tex_freeze,1);
u.press(0,100);u.release(0);u.press(0,600);u.release(0);assert.equal(params.lfo1_speed,73);params.lfo1_sync=1;u.press(0,1100);u.release(0);assert.equal(params.lfo1_sync,9);
u.press(3,0);u.release(3);u.press(3,375);u.release(3);assert.equal(params.dly_time,375);
u.press(6);u.release(6);u.press(6);u.release(6);u.press(5);u.release(5);u.press(7);u.release(7);assert.deepEqual(chosen,['B','INIT','B','INIT']);assert.equal(userPadAt(4),null);assert.equal(userPadAt(63),null);
const sent=[],s={cabine:cabineLayout({layer:'DRUMS'}),lanes:Array.from({length:9},(_,i)=>({length:64,direction:0,mul:1,div:1,mute:false,baseNote:i>=5?[36,41,48,55][i-5]:60})),sequence:Array.from({length:9},()=>Array.from({length:64},()=>({on:false,note:36,inherit:true,velocity:100,probability:1,ratchet:1,gate:.5})))};
const m=createCabineSequence({state:s,send:data=>sent.push(data),param:id=>params[id]??0,setParam:(id,v)=>params[id]=v});
for(const level of [1,2,3,0]){m.cycle(0);assert.equal(drumLevel(s.sequence[5][0]),level);}s.cabine.stepPage=3;m.cycle(63);assert.equal(s.sequence[8][63].velocity,40);assert(s.sequence[8][63].on);assert.equal(s.cabine.drum,3);
s.sequence[8][0].inherit=false;s.sequence[8][0].note=70;m.tune(1);assert.equal(s.lanes[8].baseNote,56);assert.equal(s.sequence[8][63].note,56);assert.equal(s.sequence[8][0].note,70);assert(sent.every(v=>v.type==='part'));
m.muteTap(1,1000);assert(s.lanes[6].mute);m.muteTap(1,1100);assert.equal(s.cabine.solo,1);assert(!s.lanes[6].mute);assert(s.lanes[5].mute);m.muteTap(1,2000);m.muteTap(1,2100);assert.equal(s.cabine.solo,-1);assert(!s.lanes[5].mute);assert(!s.lanes[6].mute);
s.cabine.melody=true;m.cycle(0);assert.equal(m.selected,48);m.melodyNote(67);assert.equal(s.sequence[0][48].note,67);assert.equal(s.sequence[0][48].velocity,40);assert.equal(s.sequence[0][48].inherit,false);m.clear();assert(s.sequence[0].every(e=>!e.on));m.melodyNote(69);assert.equal(s.sequence[0][48].velocity,80);
m.togglePlay();assert.equal(params.seq_on,1);assert.equal(params.seq_play,1);m.togglePlay();assert.equal(params.seq_play,0);assert.equal(params.seq_on,1);
console.log('Native CABINE workflows: USER momentary ownership/restore/freezes/TAP/presets; DRUMS strengths/64-step pages/inherited pitch/mute-solo; MELODY assignment/clear/play passed.');

const pads=Array.from({length:2},()=>({style:{},setPointerCapture(){}})),steps=[{on:false,accent:0},{on:true,accent:.5}],pub=[];
const cancel=installDrumTouch({pads,resolve:i=>({step:steps[i],lane:i}),publish:l=>pub.push(l),refresh(){},pulse(){}});
const e=(y,type='pointermove',id=1)=>({pointerId:id,pointerType:'touch',clientY:y,type,preventDefault(){}});
pads[0].onpointerdown(e(100));pads[0].onpointermove(e(40));pads[0].onpointerup(e(40,'pointerup'));assert.equal(steps[0].accent,.5);assert.equal(steps[0].on,false);assert.equal(steps[1].accent,.5);
pads[0].onpointerdown(e(100));pads[0].onpointerup(e(100,'pointerup'));assert.equal(steps[0].on,true);
pads[0].onpointerdown(e(100));pads[0].onpointercancel(e(100,'pointercancel'));assert.equal(steps[0].on,true);
pads[0].onpointerdown(e(100));cancel();pads[0].onpointerup(e(100,'pointerup'));assert.equal(steps[0].on,true);
assert(pub.every(l=>l===0));console.log('Drum touch: continuous accent, captured pad, tap toggle, cancel and teardown passed.');

m.publish(5,0);assert.equal(sent.at(-1).type,'step');assert.equal(sent.at(-1).values[0],5);assert.equal(sent.at(-1).values[1],0);assert.equal(sent.at(-1).expression,s.sequence[5][0]);
let pulses=0;const hp=[{style:{},setPointerCapture(){}}],hs={on:true,accent:0};installDrumTouch({pads:hp,resolve:()=>({step:hs,lane:5,stepIndex:0}),publish(){},refresh(){},pulse(){pulses++;}});hp[0].onpointerdown(e(100));hp[0].onpointermove(e(80));const first=pulses;for(let y=79;y>=72;y--)hp[0].onpointermove(e(y));assert.equal(pulses,first,'Small motions do not spam haptics');hp[0].onpointermove(e(20));assert.equal(pulses,first+1);hp[0].onpointermove(e(-100));const end=pulses;hp[0].onpointermove(e(-200));assert.equal(pulses,end,'A saturated endpoint pulses once');
console.log('Drum touch: compact per-step messages and bounded haptic travel/endpoints passed.');
