// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {nativeStrings,nativeFunction,nativeKnobRows,nativeRow} from './native-source.mjs';
for(const dimension of ['','[]','[9]','[kCount]']){
 const source=`// kPowerIds[1] = { "wrong" };\nconst char* kPowerIds${dimension} = { "", "a", /* "ignored" } */ "b" };`;
 assert.deepEqual(nativeStrings(source,'kPowerIds'),['','a','b']);
}
const source=`void Panel::resized() { const char* s="} // not a comment";
  if (true) { for(auto* k : { &extra }) {} }
  for (auto* k : { &cutoff, &reso }) {} // { ignored
  for(auto* k : { &atk, &dec }) {}
} void Other::resized() { for(auto* k : { &wrong }) {} }`;
const rows=nativeKnobRows(nativeFunction(source,'void Panel::resized()'));
assert.deepEqual(nativeRow(rows,'cutoff'),['cutoff','reso']);
assert.deepEqual(nativeRow(rows.reverse(),'atk'),['atk','dec']);
assert.throws(()=>nativeRow(rows,'missing'));
assert.throws(()=>nativeRow([...rows,['cutoff']],'cutoff'));
assert.throws(()=>nativeStrings(source,'missing'));
assert.throws(()=>nativeFunction('void Panel::resized() {','void Panel::resized()'));
console.log('Native source readers: appended table entries, symbolic/unsized arrays, comments, nested braces, reordered rows and missing/ambiguous declarations passed.');
