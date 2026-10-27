// SPDX-License-Identifier: GPL-3.0-only
// PLAY (docs/PLAY.md): the pure part - the organ's keys, the grid, the snapshots' mix on the XY, the looped gesture,
// the macros as CCs, the saved state. The same rules as the plugin's ui/PlayPanel (C3 = 60, the CABINE's names).
import {cabineScales} from './cabine-keys.js';
const clamp=(v,a,b)=>Math.min(b,Math.max(a,Number.isFinite(+v)?+v:a));
export const WHITE=[0,2,4,5,7,9,11],WHITES=22,GRID_COLS=16,GRID_ROWS=6,FIXED_VELOCITY=100,MACRO_CC=16,GLIDES=[0,.25,.5,1,2,4];
const names=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
export const noteName=n=>`${names[n%12]}${Math.floor(n/12)-2}`;
// a manual from its octave (C2 = 2: its lowest C 48) and its white keys (22: 3 octaves and the top C; the phone 10, the
// tablet 15 - a finger's width each); x and w in fractions of its width
export const whitesFor=width=>width<600?10:width<900?15:WHITES;
export function organKeys(octave,whites=WHITES){
  const base=(clamp(Math.round(octave),0,8)+2)*12,keys=[],ww=1/whites;
  for(let i=0;i<whites;++i){const n=base+12*Math.floor(i/7)+WHITE[i%7];if(n<128)keys.push({note:n,black:false,x:i*ww,w:ww});}
  for(let i=0;i<whites-1;++i){const k=i%7;if(k===2||k===6)continue;const n=base+12*Math.floor(i/7)+WHITE[k]+1;if(n<128)keys.push({note:n,black:true,x:(i+1)*ww-ww*.3,w:ww*.6});}
  return keys;
}
// the key under (fx, fy) in a manual (fractions of it; fy 0 the top): the blacks over the whites' top 60 %
export function organNoteAt(octave,fx,fy,whites=WHITES){
  const keys=organKeys(octave,whites);if(fx<0||fx>1||fy<0||fy>1)return null;
  if(fy<.6){const b=keys.find(k=>k.black&&fx>=k.x&&fx<k.x+k.w);if(b)return {note:b.note,rel:fy/.6};}
  const w=keys.find(k=>!k.black&&fx>=k.x&&fx<k.x+k.w);return w?{note:w.note,rel:fy}:null;
}
// the grid: the CABINE's isomorphic rule (a column a degree, a row a fourth up), row 0 at the top
export function gridPitch(row,col,{scale=1,root=0,octave=2}={}){
  const deg=cabineScales[scale]?.[1];if(!deg||row<0||row>=GRID_ROWS||col<0||col>=GRID_COLS)return -1;
  const n=deg.length,step=n===12?5:n===5?2:3,d=(GRID_ROWS-1-row)*step+col,p=(clamp(octave,0,8)+2)*12+clamp(root,0,11)+12*Math.floor(d/n)+deg[d%n];
  return p>=0&&p<128?p:-1;
}
export const velocityAt=(rel,touch)=>touch?Math.round(clamp(20+107*rel,1,127)):FIXED_VELOCITY;
// the macros at (x, y): A top left, B top right, C bottom left, D bottom right; an empty corner is 0 (the patch as it is)
export const CORNERS=[[0,0],[1,0],[0,1],[1,1]];
export function mixSnapshots(snaps,set,x,y){
  x=clamp(x,0,1);y=clamp(y,0,1);const w=[(1-x)*(1-y),x*(1-y),(1-x)*y,x*y];
  return [0,1,2,3].map(m=>clamp([0,1,2,3].reduce((v,s)=>v+((set>>s)&1?w[s]*(snaps[s]?.[m]??0):0),0),-1,1));
}
export const easeGlide=t=>{t=clamp(t,0,1);return t*t*(3-2*t);};
// the looped gesture at t seconds (its points {t, x, y}, closed back to its start)
export function loopAt(loop,t){
  if(!loop?.length)return null;if(loop.length===1)return {x:loop[0].x,y:loop[0].y};
  const len=loop.at(-1).t;if(!(len>0))return {x:loop[0].x,y:loop[0].y};
  const u=((t%len)+len)%len;let i=1;while(i<loop.length-1&&loop[i].t<u)++i;
  const a=loop[i-1],b=loop[i],f=b.t>a.t?clamp((u-a.t)/(b.t-a.t),0,1):1;
  return {x:a.x+(b.x-a.x)*f,y:a.y+(b.y-a.y)*f};
}
export function closeLoop(points){
  if(points.length<3||points.at(-1).t<=.25)return null;
  return [...points,{t:points.at(-1).t+1/60,x:points[0].x,y:points[0].y}];
}
export const macroCc=v=>Math.round((clamp(v,-1,1)+1)/2*127);   // (-1..1 -> 0..127; 0 -> 64)
// how much fits: the phone up to two manuals, the tablet and the desktop three
export const tierFor=width=>width<600?'phone':width<900?'tablet':'desktop';
export const maxManuals=width=>width<900?2:3;
export function defaultPlayState(){
  return {snaps:[[0,0,0,0],[0,0,0,0],[0,0,0,0],[0,0,0,0]],set:0,x:.5,y:.5,loop:[],layout:'organ',manuals:3,octaves:[2,3,4],vel:true,glide:2,lock:false,
    dest:'here',mpe:true,scale:1,root:0,gridOctave:2,slide:null,panel:null};   // (slide null: by the width - BEND on a phone)
}
export function parsePlayState(data){
  const s=defaultPlayState(),d=data&&typeof data==='object'?data:{};
  if(Array.isArray(d.snaps))s.snaps=[0,1,2,3].map(i=>[0,1,2,3].map(m=>clamp(d.snaps[i]?.[m]??0,-1,1)));
  s.set=clamp(Math.round(d.set??0),0,15);s.x=clamp(d.x??.5,0,1);s.y=clamp(d.y??.5,0,1);
  if(Array.isArray(d.loop))s.loop=d.loop.filter(p=>p&&Number.isFinite(+p.t)).slice(0,4000).map(p=>({t:+p.t,x:clamp(p.x,0,1),y:clamp(p.y,0,1)}));
  s.layout=d.layout==='grid'?'grid':'organ';s.manuals=clamp(Math.round(d.manuals??3),1,3);
  if(Array.isArray(d.octaves))s.octaves=[0,1,2].map(i=>clamp(Math.round(d.octaves[i]??i+2),0,8));
  s.vel=d.vel!==false;s.glide=clamp(Math.round(d.glide??2),0,GLIDES.length-1);s.lock=d.lock===true;
  s.dest=d.dest==='control'?'control':'here';s.mpe=d.mpe!==false;s.scale=clamp(Math.round(d.scale??1),0,cabineScales.length-1);
  s.slide=d.slide==='bend'||d.slide==='legato'?d.slide:null;
  s.panel=['full','compact','keys'].includes(d.panel)?d.panel:null;   // (null: compact on a short screen, else full)
  s.root=clamp(Math.round(d.root??0),0,11);s.gridOctave=clamp(Math.round(d.gridOctave??2),0,8);
  return s;
}
