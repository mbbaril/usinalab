// SPDX-License-Identifier: GPL-3.0-only
export const explicitDuration=(s,n)=>Number.isFinite(s.durations?.[n])&&s.durations[n]>0?Math.max(.01,Math.min(64,s.durations[n])):null;
export const noteDuration=(s,n,settings={})=>explicitDuration(s,n)??Math.max(.01,s.gate/Math.max(1,s.ratchet||1)*(settings.mul||1)/(settings.div||1));
export function mappedDurations(s,before,after,settings={}){
  const result={},groups=new Map();for(let i=0;i<before.length;i++){const target=after[i];if(!Number.isInteger(target))continue;if(!groups.has(target))groups.set(target,[]);groups.get(target).push(before[i]);}
  for(const [n,group]of groups)if(group.some(p=>explicitDuration(s,p)!==null))result[n]=Math.max(...group.map(p=>noteDuration(s,p,settings)));
  return Object.keys(result).length?result:undefined;
}
export function setNoteDuration(s,n,d){s.durations={...s.durations,[n]:Math.max(.01,Math.min(64,Math.round(d*100)/100))};}
export function validDurations(value){return value===undefined||value!==null&&!Array.isArray(value)&&typeof value==='object'&&Object.keys(value).length<=16&&Object.entries(value).every(([n,d])=>/^(0|[1-9][0-9]*)$/.test(n)&&Number(n)<128&&Number.isFinite(d)&&d>=.01&&d<=64);}
