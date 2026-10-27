// SPDX-License-Identifier: GPL-3.0-only
// ModPanel::showEnv/refresh: editor state, not a new sound/preset parameter.
export function createEnvelopeLink({param,setParam}) {
  let index=0,enabled=false,lastDec=-1,lastRel=-1;
  const prefixes=['env1_','eg1_','eg2_'];
  function refresh() {
    const prefix=prefixes[index],dec=param(prefix+'dec'),rel=param(prefix+'rel');
    if(enabled && lastDec>=0 && dec!==rel) {
      // The native frame gives DEC priority if both changed together.
      if(dec!==lastDec)setParam(prefix+'rel',dec);
      else if(rel!==lastRel)setParam(prefix+'dec',rel);
    }
    lastDec=dec;lastRel=rel;
  }
  return {refresh,select(value){index=Math.max(0,Math.min(2,value));lastDec=lastRel=-1;refresh();},setEnabled(value){enabled=!!value;},get enabled(){return enabled;},get index(){return index;}};
}
