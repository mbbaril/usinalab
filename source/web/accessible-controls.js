// SPDX-License-Identifier: GPL-3.0-only
export function controlName(descriptor,caption=descriptor.name){
  const op=/^op(\d+)_/.exec(descriptor.id||'');
  const suffix=op?(descriptor.id||'').slice(op[0].length):'';
  const envelope=/^(atk|dec|sus|rel|curve|rate_key|env_)/.test(suffix),oscillator=/^(mode|wave|freq|coarse|fine|detune|trans|fixed_hz)$/.test(suffix);
  const family=/^(lfo[1-3]|rand[1-3]|func[1-3]|comb[1-3]|env1|eg[12]|lpg|casc)_/.exec(descriptor.id||'');
  const group=op?`OP${op[1]}${envelope?' ENVELOPE':oscillator?' OSCILLATOR':/^[abcd]$/.test(suffix)?' PROCESSOR':''}`:/^f2_/.test(descriptor.id||'')?'FILTER 2':family?family[1].toUpperCase():descriptor.group;
  if(op&&(envelope||oscillator||/^[abcd]$/.test(suffix)))caption=caption.replace(/^OP\d+[. ]/i,'');
  const upper=caption.toUpperCase(),prefix=group?.toUpperCase(),hasPrefix=prefix&&(upper===prefix||upper.startsWith(prefix+' ')||upper.startsWith(prefix+'.'));
  return group&&!hasPrefix?`${group} ${caption}`:caption;
}
export function keyboardSlider(target,{name,get,set,min=-1,max=1,step=.01}){
  target.tabIndex=0;target.setAttribute('role','slider');target.setAttribute('aria-label',name);
  target.setAttribute('aria-valuemin',min);target.setAttribute('aria-valuemax',max);
  target.onkeydown=e=>{
    if(!['ArrowUp','ArrowRight','ArrowDown','ArrowLeft','Home','End'].includes(e.key))return;
    e.preventDefault();
    const next=e.key==='Home'?min:e.key==='End'?max:get()+(['ArrowUp','ArrowRight'].includes(e.key)?1:-1)*step*(e.shiftKey?.1:1);
    if(Number.isFinite(next))set(Math.max(min,Math.min(max,next)));
  };
}
