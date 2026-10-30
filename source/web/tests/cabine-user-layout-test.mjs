// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {userPadGroups,userPadAt,renderUserPads,createUserPerformance} from '../cabine-user.js';
const expected=Array.from({length:64},(_,i)=>i).filter(i=>userPadAt(i));const grouped=userPadGroups.flatMap(g=>g.indices);
assert.equal(new Set(grouped).size,grouped.length);assert.deepEqual([...grouped].sort((a,b)=>a-b),expected,'every native USER action appears exactly once');
function el(tag,cls='',text=''){
 const attrs={},classes=new Set(cls.split(' ')),captures=new Set();
 return {tag,className:cls,textContent:text,children:[],dataset:{},style:{setProperty(){}},classList:{toggle(k,v){v?classes.add(k):classes.delete(k);}},append(...nodes){for(const node of nodes){if(node.parent){node.parent.children.splice(node.parent.children.indexOf(node),1);}node.parent=this;this.children.push(node);}},replaceChildren(...nodes){this.children.forEach(n=>n.parent=null);this.children=[];this.append(...nodes);},setAttribute(k,v){attrs[k]=String(v);},getAttribute:k=>attrs[k],setPointerCapture:id=>captures.add(id),hasPointerCapture:id=>captures.has(id),releasePointerCapture:id=>captures.delete(id)};
}
const params={macro1:.3},root=el('section'),tools=el('div','cabine-tools');root.querySelector=()=>tools;root.append(tools);
const performance=createUserPerformance({normalized:id=>params[id]??.5,setNormalized:(id,v)=>params[id]=v,param:()=>120,setParam(){},library:()=>[],choosePreset(){},state:{}});
renderUserPads({root,el,performance,pulse(){}});
const find=(node,predicate)=>{if(predicate(node))return node;for(const child of node.children){const found=find(child,predicate);if(found)return found;}return null;};
const deck=find(root,n=>n.className==='cabine-user-deck'),grid=find(root,n=>n.className==='cabine-grid');assert.equal(grid.hidden,true);assert.equal(deck.children.length,6);
const pad=find(deck,n=>n.dataset.pad===16),matrix=find(tools,n=>n.textContent==='GRADE 8×8'),panels=find(tools,n=>n.textContent==='GRUPOS');
const touch={pointerType:'touch',button:0,pointerId:5,preventDefault(){}};pad.onpointerdown(touch);assert.equal(params.macro1,.125);assert.equal(pad.dataset.gestureHint,'SOLTE ↶');
matrix.onclick();assert.equal(params.macro1,.3,'changing presentation releases momentary values');assert.equal(performance.held.size,0);assert.equal(grid.children.length,64);assert.equal(grid.children[16],pad,'same pad and native index survive reparenting');pad.onpointerup(touch);assert.equal(params.macro1,.3,'late release cannot overwrite restored state');
panels.onclick();assert.equal(find(deck,n=>n.dataset.pad===16),pad);assert.equal(pad.textContent,'12.5%');assert.equal(grid.hidden,true);
console.log('USER groups: complete native action coverage, no duplicates, stable pad IDs, grid switch, release on presentation change and late-event protection passed.');
