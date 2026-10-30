// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync} from 'node:fs';
import {createCabineActions} from '../cabine-actions.js';
import {exportMappings,importMappings} from '../mapping-files.js';
const require=createRequire(new URL('../../mobile/package.json',import.meta.url)),{DOMParser}=require('@xmldom/xmldom');
class Parser extends DOMParser {constructor(){super({onError:(_level,message)=>{throw Error(message);}});}}
const meta=JSON.parse(readFileSync(new URL('../../build-web/dist/metadata.json',import.meta.url))),state={ccMaps:[{param:'cutoff',cc:74,channel:-1}],controllerBindings:[{kind:'note',number:40,channel:15}],mappingPorts:{input:'third party & MIDI',output:'Out'}};
const calls=[],a=createCabineActions({state,run:(...v)=>calls.push(v)});
a.learn(10);assert(!a.handle({status:144,a:60,b:0}));assert.equal(a.learning,10);assert(a.handle({status:146,a:60,b:100,timestamp:1}));assert.equal(a.learning,-1);assert.deepEqual(state.actionBindings[10],{kind:'note',number:60,channel:2});assert.deepEqual(calls.at(-1).slice(0,4),[10,true,100/127,false]);
assert(!a.handle({status:145,a:60,b:100}));a.handle({status:162,a:60,b:80});assert.deepEqual(calls.at(-1).slice(0,4),[10,true,80/127,true]);a.handle({status:130,a:60,b:0});assert.deepEqual(calls.at(-1).slice(0,3),[10,false,0]);
a.handle({status:146,a:60,b:100});a.forget(10);assert.deepEqual(calls.at(-1).slice(0,3),[10,false,0]);assert(!state.actionBindings[10]);
a.learn(8);a.handle({status:176,a:20,b:127});a.cancel();assert.deepEqual(calls.at(-1).slice(0,3),[8,false,0]);state.actionBindings[8].channel=-1;a.handle({status:191,a:20,b:1});assert.equal(calls.at(-1)[0],8);
const text=exportMappings(state),round=importMappings(text,meta,Parser);assert.equal(round.ccMaps[0].channel,-1);assert.equal(round.controllerBindings[0].channel,15);assert.equal(round.actionBindings[8].channel,-1);assert.equal(round.mappingPorts.input,'third party & MIDI');assert.equal(round.actionBindings.length,16);assert.equal(round.controllerBindings.length,8);
const legacy=importMappings(text.replace('<PADGRID','<LAUNCHPAD'),meta,Parser);assert(text.includes('<PADGRID'));assert.equal(legacy.mappingPorts.input,'third party & MIDI');
const unknown=importMappings(text.replace('id="cutoff"','id="future-parameter"'),meta,Parser);assert.equal(unknown.ignored,1);assert.equal(unknown.ccMaps.length,0);
for(const invalid of ['<USINA/>','<!DOCTYPE MAPPINGS><MAPPINGS/>',text.replace('ch="0"','ch="17"'),text.replace('<CCMAP>','<CCMAP><OTHER/>')])assert.throws(()=>importMappings(invalid,meta,Parser));
const credits=readFileSync(new URL('../credits.js',import.meta.url),'utf8'),notices=readFileSync(new URL('../THIRD_PARTY-NOTICES.txt',import.meta.url),'utf8');assert(!/D:\\|src\/|tools\/|jeito certo|THIRD_PARTY\.md/i.test(credits+notices));assert(notices.includes('Copyright 2015 Matthias Puech.'));assert(notices.includes('Permission is hereby granted'));
console.log('MIDI maps: Learn/Forget, immediate trigger, channel filtering/ANY, poly pressure, release/cancel, JUCE XML round trip, unknown IDs and malformed imports passed. Credits contain attribution/license notices without internal paths or editorial comments.');
