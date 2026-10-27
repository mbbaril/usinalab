// SPDX-License-Identifier: GPL-3.0-only
const clamp=(n,a,b)=>Math.max(a,Math.min(b,n));
export function responsePath(values){let open=false;return values.map((db,i)=>{if(!Number.isFinite(db)){open=false;return '';}const part=`${open?'L':'M'}${(i*360/(values.length-1)).toFixed(2)} ${clamp((48-db)/120*170,0,170).toFixed(2)}`;open=true;return part;}).join('');}
// Interpolation is visual only, between measured responses; the endpoint is the unmodified measurement.
export function createResponseCurve({write,request=requestAnimationFrame,cancel=cancelAnimationFrame,clock=()=>performance.now()}){
  let frame=0,shown=null;
  const stop=()=>{if(frame)cancel(frame);frame=0;};
  return {set(values){stop();const to=Array.from(values);if(!shown){shown=to;write(responsePath(shown));return;}const from=shown.slice(),start=clock();const tick=()=>{const t=clamp((clock()-start)/100,0,1);shown=to.map((v,i)=>Number.isFinite(v)&&Number.isFinite(from[i])?from[i]+(v-from[i])*t:v);write(responsePath(shown));frame=t<1?request(tick):0;};frame=request(tick);},clear(path=''){stop();shown=null;write(path);},stop};
}
