// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {acceptsMusicalTyping,installTypingKeyboard,releasePointerChoiceFocus} from '../typing-keyboard.js';
import {KeybedState} from '../keybed-state.js';
class Target{constructor(tagName='BUTTON',type){this.tagName=tagName;this.type=type;this.events={};this.blurred=false;}addEventListener(k,f){(this.events[k]??=[]).push(f);}removeEventListener(k,f){this.events[k]=this.events[k].filter(n=>n!==f);}emit(k,e={}){this.events[k]?.forEach(f=>f(e));}blur(){this.blurred=true;}}
const window=new Target(),sent=[],keys=new KeybedState(packets=>sent.push(...packets));let base=48,escaped=0;
const dispose=installTypingKeyboard({target:window,base:()=>base,press:(id,n)=>keys.press(id,n),release:id=>keys.release(id),escape:()=>escaped++});
const event=(code,target=new Target(),extras={})=>({code,target,preventDefault(){this.prevented=true;},...extras});
for(const code of ['KeyA','KeyS','KeyD','KeyF']){const e=event(code);window.emit('keydown',e);assert(e.prevented);}assert.deepEqual(sent.map(p=>p[1]),[48,50,52,53]);
window.emit('keydown',event('KeyA',undefined,{repeat:true}));assert.equal(sent.length,4);
base=60;for(const code of ['KeyA','KeyS','KeyD','KeyF'])window.emit('keyup',event(code,new Target('INPUT','text')));assert.deepEqual(sent.slice(4).map(p=>p[1]),[48,50,52,53]);assert.equal(keys.keys.size,0);
for(const node of [new Target('INPUT','search'),new Target('INPUT','number'),new Target('TEXTAREA'),new Target('SELECT'),Object.assign(new Target('SPAN'),{isContentEditable:true})])assert(!acceptsMusicalTyping(event('KeyA',node)));
for(const extra of [{ctrlKey:true},{metaKey:true},{altKey:true},{isComposing:true},{defaultPrevented:true}])assert(!acceptsMusicalTyping(event('KeyA',undefined,extra)));
assert(acceptsMusicalTyping(event('KeyA',new Target('INPUT','range'))));window.emit('keydown',event('Escape'));assert.equal(escaped,1);
const choice=new Target('SELECT');releasePointerChoiceFocus(choice);choice.emit('pointerdown');choice.emit('change');assert(choice.blurred);choice.blurred=false;choice.emit('pointerdown');choice.emit('keydown');choice.emit('change');assert(!choice.blurred,'keyboard choice editing keeps focus');
window.emit('keydown',event('',new Target(),{key:'a'}));assert.equal(sent.at(-1)[1],60);window.emit('keyup',event('Unidentified',new Target('INPUT','text'),{key:'A'}));assert.deepEqual(sent.at(-1),[0x80,60,0]);assert.equal(keys.keys.size,0);
dispose();assert.equal(window.events.keydown.length,0);assert.equal(window.events.keyup.length,0);
console.log('Musical typing: ASDF after button focus, exact key-up after octave/focus changes, text/select/composition/shortcut isolation and pointer-choice focus release passed.');
