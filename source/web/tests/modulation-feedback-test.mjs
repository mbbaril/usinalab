// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {modulationTravel,modulationArc} from '../modulation-feedback.js';
import {VisualSourcePublisher} from '../visual-sources.js';
const meta=JSON.parse(readFileSync(new URL('../../build-web/dist/metadata.json',import.meta.url),'utf8'));let checks=0;
for(const row of readFileSync(new URL('../../build-web/parity/mod-ui-native.tsv',import.meta.url),'utf8').trim().split(/\r?\n/)){
  const[id,base,low,high,live,spec]=row.split('\t'),values=new Float32Array(128),routes=spec.split(';').map(r=>{const [source,amount,value]=r.split(',').map(Number);values[source]=value;return {source,amount};}),d=meta.parameters.find(d=>d.id===id),v=modulationTravel(d,+base,routes,meta.sources,values);
  for(const[k,expected]of [['low',low],['high',high],['live',live]])assert(Math.abs(v[k]-Number(expected))<1e-5,`${id} ${base} ${k}: ${v[k]} vs ${expected}`);checks++;
  assert(!modulationArc(v.low,v.high).includes('NaN'));
}
assert.equal(checks,210,'all six scenarios at five positions of seven native parameters exported');
const d={id:'cutoff',min:0,max:1000},routes=[{source:meta.sources.find(s=>s.id==='lfo1').index,amount:.25}];
assert.equal(modulationTravel(d,500,routes,meta.sources,null).live,null,'no invented live value before telemetry');
const sent=[],pub=new VisualSourcePublisher(packet=>sent.push({token:packet.token,count:packet.count,values:Array.from(packet.values)}),id=>id/10);
pub.configure([1,1,2,-1,128,NaN,3],7);for(let i=0;i<10000;i++)pub.tick(128,i,48000);assert.equal(sent.length,1,'bounded backpressure while UI does not ack');assert.equal(sent[0].count,3);assert.deepEqual(sent[0].values.filter((_,i)=>i<6&&i%2===0),[1,2,3]);
pub.ack(6);pub.tick(1600,1,48000);assert.equal(sent.length,1,'old ACK cannot release new subscription');pub.ack(7);pub.tick(1600,2,48000);assert.equal(sent.length,2);
pub.configure([],8);pub.ack(7);pub.tick(99999,3,48000);assert.equal(sent.length,2,'no readings when no visible destinations');
pub.configure([4],9);pub.tick(1600,4,48000);assert.equal(sent.at(-1).token,9);assert.equal(sent.at(-1).values[0],4);
const hash=p=>createHash('sha256').update(readFileSync(new URL(p,import.meta.url))).digest('hex');
writeFileSync(new URL('../../build-web/parity/modulation-feedback-report.json',import.meta.url),JSON.stringify({passed:true,checks,nativeControlsSha256:hash('../../src/plugin/ui/Controls.cpp'),nativeLookSha256:hash('../../src/plugin/ui/UsinaLook.cpp'),nativeModSha256:hash('../../src/engines/fm/FmMod.h'),viewSha256:hash('../modulation-feedback.js'),publisherSha256:hash('../visual-sources.js'),scope:'Compared to actual native Knob modLo/modHi/modLive properties; bounded read-only telemetry and absent-data behavior.'},null,2));
console.log(`${checks} real native knob modulation-range/live-dot cases, polarity/saturation and bounded telemetry passed.`);
