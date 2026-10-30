// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {bindPopupMenu} from '../popup-keyboard.js';
import {el,Node} from './performance-dom.mjs';
const panel=el('div'),buttons=[el('button','','Learn'),el('button','','Forget'),el('button','','Close')],closed=[];buttons[1].disabled=true;panel.append(...buttons);bindPopupMenu(panel,buttons,restore=>closed.push(restore));
buttons[0].focus();panel.fire('keydown',{key:'ArrowDown',target:buttons[0]});assert.equal(Node.active,buttons[2]);panel.fire('keydown',{key:'ArrowDown',target:buttons[2]});assert.equal(Node.active,buttons[0]);panel.fire('keydown',{key:'End',target:buttons[0]});assert.equal(Node.active,buttons[2]);panel.fire('keydown',{key:'Home',target:buttons[2]});assert.equal(Node.active,buttons[0]);panel.fire('keydown',{key:'ArrowUp',target:buttons[0]});assert.equal(Node.active,buttons[2]);panel.fire('keydown',{key:'Tab',target:buttons[2]});assert.deepEqual(closed,[true]);
const before=Node.active;panel.fire('keydown',{key:'a',target:buttons[2]});assert.equal(Node.active,before);
console.log('Popup menu: disabled-item skipping, arrows, Home/End, wrapping, Tab dismissal and unrelated-key isolation passed.');
