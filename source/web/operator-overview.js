// SPDX-License-Identifier: GPL-3.0-only
import {operatorUsesWave} from './operator-layout.js';
export function mountOperatorOverview({el,strip,prefix,mode,descriptor,field}){
  const choices=el('div','operator-overview-choices');
  for(const key of ['mode','wave']){const control=field({...descriptor(prefix+key),uiCaption:key.toUpperCase(),instance:'overview',dim:key==='wave'&&!operatorUsesWave(mode)});
    control.querySelector?.('select')?.setAttribute('aria-label',`${prefix.slice(0,-1).toUpperCase()} ${key.toUpperCase()}`);choices.append(control);}
  strip.append(choices);return choices;
}
