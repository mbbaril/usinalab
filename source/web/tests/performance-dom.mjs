// SPDX-License-Identifier: GPL-3.0-only
export class Node {
  constructor(tag='div',cls='',text=''){this.tagName=tag.toUpperCase();this.className=cls;this.textContent=text;this.children=[];this.dataset={};this.attrs={};this.events=new Map();this.style={setProperty(){}};this.clientWidth=891;this.clientHeight=300;this.value='';this.classList={add:(...names)=>{this.className+=' '+names.join(' ');},remove:(...names)=>{this.className=this.className.split(' ').filter(n=>!names.includes(n)).join(' ');},contains:n=>this.className.split(' ').includes(n),toggle:(n,on)=>{on??=!this.classList.contains(n);on?this.classList.add(n):this.classList.remove(n);}};}
  append(...nodes){this.children.push(...nodes);}
  replaceChildren(...nodes){this.children=nodes;}
  get options(){return this.children;}
  setAttribute(k,v){this.attrs[k]=String(v);}
  getAttribute(k){return this.attrs[k]??null;}
  addEventListener(k,f){if(!this.events.has(k))this.events.set(k,new Set());this.events.get(k).add(f);}
  removeEventListener(k,f){this.events.get(k)?.delete(f);}
  fire(type,extra={}){const e={type,key:'',preventDefault(){this.defaultPrevented=true;},stopPropagation(){},...extra};this['on'+type]?.(e);for(const f of [...this.events.get(type)||[]])f(e);return e;}
  focus(){if(Node.active!==this)Node.active?.fire('blur');Node.active=this;this.fire('focus');}
  matches(s){if(s.startsWith('#'))return this.id===s.slice(1);if(s.startsWith('.'))return this.classList.contains(s.slice(1));if(s==='[data-note]')return this.dataset.note!==undefined;return s==='button'?this.tagName==='BUTTON':s==='select'?this.tagName==='SELECT':s==='output'?this.tagName==='OUTPUT':this.tagName===s.toUpperCase();}
  querySelectorAll(s){return this.children.flatMap(n=>[...(n.matches(s)?[n]:[]),...n.querySelectorAll(s)]);}
  querySelector(s){return this.querySelectorAll(s)[0]||null;}
  getBoundingClientRect(){return {left:0,top:0,width:this.clientWidth,height:this.clientHeight};}
  setPointerCapture(){}hasPointerCapture(){return true;}
}
export const el=(...args)=>new Node(...args);
export function environment(state={}){
  const win=new Node(),doc=new Node();doc.createElementNS=(_,tag)=>new Node(tag);doc.hidden=false;globalThis.window=win;globalThis.document=doc;win.innerHeight=800;
  globalThis.localStorage={getItem:()=>JSON.stringify(state),setItem(){}};globalThis.requestAnimationFrame=()=>1;globalThis.addEventListener=win.addEventListener.bind(win);globalThis.removeEventListener=win.removeEventListener.bind(win);return {win,doc};
}
