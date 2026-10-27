// SPDX-License-Identifier: GPL-3.0-only
import {transposeChord,quantizeNote} from './sequence-harmony.js';
import {mappedDurations} from './note-duration.js';
export const stepSize=12,laneSize=8+64*stepSize,wireSize=9*laneSize;
export const conditionNames=['ALL','1:2','2:2','1:3','2:3','3:3','1:4','2:4','3:4','4:4','1ST'];
export function encodeSequence(state,out){for(let l=0;l<9;l++){const p=state.lanes[l],base=p.baseNote??(l>=5?[36,41,48,55][l-5]:60),row=l*laneSize;out.set([p.length,p.direction,p.mul,p.div,+p.mute,base,p.scale??-1,p.root??0],row);for(let i=0;i<64;i++){const s=state.sequence[l][i];out.set([+s.on,s.note,s.velocity,s.probability,s.ratchet,s.gate,s.accent??0,s.late??0,s.condition??0,s.pitchOffset??0,+!!s.inherit,0],row+8+i*stepSize);}}}
export function decodeLane(wire,lane,before){return Array.from({length:64},(_,i)=>{const v=wire.subarray(lane*laneSize+8+i*stepSize,lane*laneSize+8+(i+1)*stepSize);return {...before[i],on:!!v[0],note:v[1],velocity:v[2],probability:v[3],ratchet:v[4],gate:v[5],accent:v[6],late:v[7],condition:v[8],pitchOffset:v[9],inherit:!!v[10]};});}
export function reconcileChords(before,after,operation,policy,length,scale=null,root=0){
  for(let i=0;i<64;i++){
    if(operation===4||operation===5){const by=operation===4?-1:1,from=i<length?(i-by+length)%length:i;after[i].durations=before[from].durations?{...before[from].durations}:undefined;after[i].notes=before[from].notes?[...before[from].notes]:undefined;continue;}
    if(operation===0||operation===3||i>=length)continue;
    const original=before[i];if(!original.notes?.length)continue;
    if(operation===6){const next=original.notes.map(n=>quantizeNote(n+(original.pitchOffset??0),scale,root));after[i].durations=mappedDurations(original,original.notes,next);after[i].notes=[...new Set(next)].sort((a,b)=>a-b);after[i].note=after[i].notes[0];after[i].pitchOffset=0;after[i].inherit=false;continue;}
    if(policy==='mono'){after[i].notes=undefined;after[i].durations=undefined;continue;}
    if(policy!=='intervals')throw Error('Escolha como tratar os acordes antes de gerar.');
    const transposed=transposeChord(original.notes,original.note,after[i].note+(after[i].pitchOffset??0),scale,root);after[i].durations=mappedDurations(original,original.notes,transposed.notes);after[i].notes=transposed.notes;after[i].note=transposed.note;after[i].pitchOffset=0;after[i].inherit=false;
  }return after;
}
