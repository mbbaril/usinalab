// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {snapshotPresentation,motionPresentation,installSnapshotPress} from '../play-feedback.js';
import {userPadBehavior,userPadAt} from '../cabine-user.js';
assert.equal(snapshotPresentation({stored:false,name:'A'}).caption,'Vazio');assert.equal(snapshotPresentation({stored:true,current:true,name:'A'}).caption,'Aqui');assert.match(snapshotPresentation({stored:true,capture:true,name:'B'}).label,/Guardar/);
assert.equal(motionPresentation({recording:true,armed:true,looping:true}).phase,'recording');assert.equal(motionPresentation({armed:true}).phase,'armed');assert.equal(motionPresentation({gliding:true}).phase,'transition');assert.equal(motionPresentation({looping:true}).phase,'loop');assert.match(motionPresentation({locked:true}).text,/bloqueada/);
let pending=new Map(),next=0,saves=0,recalls=0,stored=false,capture=false;const target={};
const cancel=installSnapshotPress({target,store:()=>{saves++;stored=true;},recall:()=>recalls++,stored:()=>stored,capturing:()=>capture,schedule:f=>{pending.set(++next,f);return next;},unschedule:id=>pending.delete(id)});
const event=(extra={})=>({button:0,pointerId:1,clientX:0,clientY:0,preventDefault(){},...extra});
target.onclick();assert.equal(saves,1);target.onclick();assert.equal(recalls,1);capture=true;target.onclick();assert.equal(saves,2);capture=false;
target.onpointerdown(event());for(const f of [...pending.values()])f();pending.clear();target.onpointerup(event());target.onclick();assert.equal(saves,3);assert.equal(recalls,1,'long hold must not recall after storing');
target.onpointerdown(event());target.onpointermove(event({clientX:20}));assert.equal(pending.size,0);target.onpointerup(event());target.onclick();assert.equal(recalls,1,'dragging off a snapshot must not recall');
target.onpointerdown(event());cancel();assert.equal(pending.size,0,'leaving cancels delayed snapshot writes');
assert.equal(userPadBehavior(userPadAt(0)),'tap');assert.equal(userPadBehavior(userPadAt(5)),'action');assert.equal(userPadBehavior(userPadAt(8)),'hold');assert.equal(userPadBehavior(userPadAt(10)),'hold');assert.equal(userPadBehavior(userPadAt(16)),'hold');assert.equal(userPadBehavior(userPadAt(48)),'hold');assert.equal(userPadBehavior(null),'');
console.log('PLAY feedback: snapshot states, capture/recall, long hold, movement and teardown; USER momentary/action/TAP hints passed.');
