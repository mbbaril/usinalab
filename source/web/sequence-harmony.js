// SPDX-License-Identifier: GPL-3.0-only
// The interval sets come from RuidoCore's metadata, never from a second scale catalogue.
export function inScale(note,scale,root=0){return !scale||scale.intervals.includes(((note-root)%12+12)%12);}
export function quantizeNote(note,scale,root=0,min=0,max=127){let best=Math.max(min,Math.min(max,Math.round(note))),distance=Infinity;if(!scale)return best;for(let n=min;n<=max;n++)if(inScale(n,scale,root)&&Math.abs(n-note)<distance){best=n;distance=Math.abs(n-note);}return best;}
export function nextScaleNote(note,delta,scale,root=0){if(!scale||Math.abs(delta)!==1)return quantizeNote(note+delta,scale,root);const start=quantizeNote(note,scale,root);for(let n=start+delta;n>=0&&n<=127;n+=delta)if(inScale(n,scale,root))return n;return start;}
export function transposeChord(notes,anchor,target,scale,root=0){let shift=0,distance=Infinity;for(let d=-Math.min(...notes);d<=127-Math.max(...notes);d++){const next=anchor+d;if(inScale(next,scale,root)&&Math.abs(next-target)<distance){shift=d;distance=Math.abs(next-target);}}return {note:anchor+shift,notes:notes.map(n=>n+shift)};}
