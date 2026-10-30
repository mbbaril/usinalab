// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {createFxSlotActions} from '../fx-slot-actions.js';
import {swapFxSlots} from '../fx-panel.js';
import {el,Node} from './performance-dom.mjs';
const values={a:1,b:6,c:0},operations=[],set=(id,v)=>{values[id]=v;for(const key of Object.keys(values))if(key!==id&&v&&values[key]===v)values[key]=0;};
const root=createFxSlotActions({el,slots:[{id:'a',label:'FX A slot 1'},{id:'b',label:'FX B slot 1'},{id:'c',label:'AUX slot 1'}],selected:'a',param:id=>values[id],exchange:(a,b,kind)=>{swapFxSlots(a,b,id=>values[id],set);operations.push(kind);}});
const [from,to]=root.querySelectorAll('select'),[move,swap]=root.querySelectorAll('button');
assert.equal(from.getAttribute('aria-label'),'Slot de origem');assert.equal(to.value,'c');assert(!move.disabled);assert(swap.disabled);move.onclick();assert.deepEqual(values,{a:0,b:6,c:1});
from.value='c';to.value='b';to.onchange();assert(move.disabled);assert(!swap.disabled);swap.onclick();assert.deepEqual(values,{a:0,b:1,c:6});assert.deepEqual(operations,['move','swap']);
from.value='a';from.onchange();assert(move.disabled&&swap.disabled);from.value='c';to.value='c';to.onchange();assert(move.disabled&&swap.disabled);
root.open=true;root.onkeydown({key:'Escape',preventDefault(){}});assert.equal(root.open,false);assert.equal(Node.active,root.children[0]);
console.log('FX slot actions: named destinations, empty-only move, occupied-only swap, deduplication-safe exchange, invalid origins and Escape focus passed.');
