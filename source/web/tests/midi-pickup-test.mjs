// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {MidiPickup} from '../midi-pickup.js';
const p=new MidiPickup(),map={param:'cutoff'};
assert.equal(p.next(map,0,.75),null);assert(p.waiting(map));
assert.equal(p.next(map,.5,.75),null);assert.equal(p.next(map,.8,.75),.8);p.applied(map,.8);
assert.equal(p.next(map,.4,.8),.4);p.applied(map,.4);assert(!p.waiting(map));
assert.equal(p.next(map,.1,.9),null);assert(p.waiting(map));
assert.equal(p.next(map,.9,.9),.9);p.applied(map,.9);
p.reset('cutoff');assert(p.waiting(map));assert.equal(p.next(map,.2,.5),null);
assert.equal(p.next(map,.503,.5),.503);p.applied(map,.5);assert.equal(p.next(map,.51,.5),.51);
p.reset();assert.equal(p.runtime.size,0);
console.log('Pickup: no jump, crossing, proximity, external edit, quantized acknowledgement and reset passed.');
