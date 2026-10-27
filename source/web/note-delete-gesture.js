// SPDX-License-Identifier: GPL-3.0-only
export class NoteDeleteGesture{
  clear(){this.previous=null;}
  tap(key,x,y,time){const p=this.previous;this.previous={key,x,y,time};if(p&&p.key===key&&time>=p.time&&time-p.time<=350&&Math.hypot(x-p.x,y-p.y)<=12){this.clear();return true;}return false;}
}
