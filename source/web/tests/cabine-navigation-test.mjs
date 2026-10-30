// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {cabineSurfaces,cabineSurface,selectCabineSurface} from '../cabine-navigation.js';
import {cabineLayout} from '../cabine-keys.js';
import {cabineActions} from '../cabine-actions.js';
assert.deepEqual(cabineSurfaces,['ROTAS','SONS','GESTOS','DRUMS','MELODY','KEYS','USER']);
for(const origin of cabineSurfaces)for(const target of cabineSurfaces){
 const c=cabineLayout({page:1,root:7,scale:4,octave:3,drum:2,stepPage:3,solo:1,mutes:[true,false,true,false]});
 selectCabineSurface(c,origin);const before=structuredClone(c);
 assert.equal(selectCabineSurface(c,target),origin!==target);
 assert.equal(cabineSurface(c),target);
 const restored=cabineLayout(JSON.parse(JSON.stringify(c)));assert.equal(cabineSurface(restored),target);
 for(const key of ['root','scale','octave','drum','stepPage','solo','mutes'])assert.deepEqual(c[key],before[key]);
 const selected=structuredClone(c);assert.equal(selectCabineSurface(c,target),false);assert.deepEqual(c,selected);
}
const drums=cabineLayout({layer:'DRUMS',melody:true});assert.equal(cabineSurface(drums),'MELODY');
selectCabineSurface(drums,'DRUMS');assert.equal(drums.melody,false);selectCabineSurface(drums,'DRUMS');assert.equal(drums.melody,false);
const invalid=structuredClone(drums);assert.throws(()=>selectCabineSurface(drums,'unknown'),RangeError);assert.deepEqual(drums,invalid);
assert.deepEqual(cabineActions.slice(0,5),['MODE ROTAS','MODE SONS','MODE GESTOS','PAGE <','PAGE >']);
console.log('CABINE navigation: 49 transitions, session restore, active-surface idempotence, performance state and MIDI action IDs preserved.');

const fs=await import('node:fs/promises'),crypto=await import('node:crypto');
const hash=async path=>crypto.createHash('sha256').update(await fs.readFile(new URL(path,import.meta.url))).digest('hex');
const report={passed:true,transitions:49,navigationSha256:await hash('../cabine-navigation.js'),panelSha256:await hash('../cabine.js'),layoutSha256:await hash('../instrument.css'),actionsSha256:await hash('../cabine-actions.js')};
await fs.mkdir(new URL('../../build-web/parity/',import.meta.url),{recursive:true});
await fs.writeFile(new URL('../../build-web/parity/cabine-navigation-report.json',import.meta.url),JSON.stringify(report,null,2));
