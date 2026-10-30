// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createEnvelopeLink} from '../envelope-link.js';
const native=readFileSync(new URL('../../src/plugin/ui/Panels.cpp',import.meta.url),'utf8');
assert(native.includes('if (dNow != lastDec) setTo(idx[3], dNow);'));assert(native.includes('else if (rNow != lastRel) setTo(idx[1], rNow);'));
const values={env1_dec:10,env1_rel:20,eg1_dec:30,eg1_rel:40,eg2_dec:50,eg2_rel:60},writes=[];
const model=createEnvelopeLink({param:id=>values[id],setParam:(id,v)=>{values[id]=v;writes.push([id,v]);}});
model.refresh();model.setEnabled(true);model.refresh();assert.equal(writes.length,0,'Enabling LINK does not equalize an unchanged pair');
values.env1_dec=11;model.refresh();assert.equal(values.env1_rel,11);model.refresh();assert.equal(writes.length,1,'Readback must settle without repeated writes');
values.env1_rel=21;model.refresh();assert.equal(values.env1_dec,21);model.refresh();
values.env1_dec=12;values.env1_rel=22;model.refresh();assert.equal(values.env1_rel,12,'DEC wins simultaneous changes, including presets');model.refresh();
for(const [i,p]of ['env1_','eg1_','eg2_'].entries()){
  const before=writes.length;model.select(i);assert.equal(writes.length,before,'Selecting a different envelope preserves its values');assert(model.enabled);
  values[p+'rel']=71+i;model.refresh();assert.equal(values[p+'dec'],71+i);model.refresh();
}
assert.equal(values.env1_rel,71);assert.equal(values.eg1_rel,72);assert.equal(values.eg2_rel,73);
model.setEnabled(false);values.eg2_dec=90;model.refresh();assert.equal(values.eg2_rel,73);model.setEnabled(true);model.refresh();assert.equal(values.eg2_rel,73);
assert(!('enabled' in values));assert(!Object.keys(values).some(k=>/^op/.test(k)));
console.log('LINK DEC/REL: native enable/selection semantics, bidirectional following, DEC priority, settling and no preset/operator fields passed.');
