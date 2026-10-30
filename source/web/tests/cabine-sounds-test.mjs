// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {renderCabineSounds} from '../cabine-sounds.js';
const el=(tag,cls='',text='')=>{const attrs={},classes=new Set();return{tag,className:cls,textContent:text,children:[],dataset:{},style:{setProperty(){}},append(...nodes){this.children.push(...nodes);},replaceChildren(...nodes){this.children=nodes;},setAttribute(k,v){attrs[k]=String(v);},getAttribute:k=>attrs[k],classList:{toggle(k,v){v?classes.add(k):classes.delete(k);}}};};
const find=(root,predicate)=>{if(predicate(root))return root;for(const c of root.children){const result=find(c,predicate);if(result)return result;}return null;};
const library=[{name:'A',cat:'bass',desc:{pt:'metal',en:'steel'}},{name:'B',cat:'pad',desc:{pt:'ar'}}],prefs={category:'',query:''},root=el('main'),values={macro1:.25,macro2:.5};let current='A',chosen=[];
const catalog=renderCabineSounds({root,el,library,current:()=>current,choose:p=>chosen.push(p),preferences:prefs,normalized:id=>values[id],setNormalized:(id,v)=>values[id]=v,color:()=>'',storage:{getItem:key=>{assert.equal(key,'usina.favourites.v1');return '["B"]';}}});
const categories=find(root,n=>n.className==='cabine-sounds-categories'),list=find(root,n=>n.className==='cabine-sounds-list'),search=find(root,n=>n.tag==='input');
assert.equal(categories.children.length,22);assert.equal(list.children.length,2);assert.equal(list.children[0].getAttribute('aria-selected'),'true');
categories.children.find(b=>b.dataset.soundCategory==='favourites').onclick();assert.equal(list.children.length,1);assert.equal(list.children[0].getAttribute('aria-label'),'B');assert.equal(chosen.length,0,'filtering never changes the sound');list.children[0].onclick();assert.equal(chosen[0],library[1],'choosing uses the original preset object');
categories.children.find(b=>b.dataset.soundCategory==='').onclick();search.value='steel';search.oninput();assert.equal(list.children.length,1);assert.equal(list.children[0].getAttribute('aria-label'),'A');assert.equal(prefs.query,'steel');
search.value='not found';search.oninput();assert.equal(list.children[0].className,'cabine-sounds-empty');
const macro=find(root,n=>n.getAttribute('aria-label')==='Macro 1 25 por cento normalizado');macro.onclick();assert.equal(values.macro1,0);macro.onclick();assert.equal(values.macro1,.25,'native macro shortcut toggles preserved');
console.log('SONS catalogue: shared favourite key, 20 categories, PT/EN filtering without applying sound, original preset identity, empty result and macro shortcuts passed.');
