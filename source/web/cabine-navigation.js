// SPDX-License-Identifier: GPL-3.0-only
// Surface navigation adapts the existing session representation; MIDI action IDs stay unchanged.
export const cabineSurfaces=Object.freeze(['ROTAS','SONS','GESTOS','DRUMS','MELODY','KEYS','USER']);
export function cabineSurface(config){
 if(config.layer==='SESSION')return ['ROTAS','SONS','GESTOS'].includes(config.mode)?config.mode:'ROTAS';
 return config.layer==='DRUMS'?(config.melody?'MELODY':'DRUMS'):['KEYS','USER'].includes(config.layer)?config.layer:'ROTAS';
}
export function selectCabineSurface(config,surface){
 if(!cabineSurfaces.includes(surface))throw new RangeError('Superfície da CABINE inválida.');
 if(cabineSurface(config)===surface)return false;
 if(['ROTAS','SONS','GESTOS'].includes(surface)){config.layer='SESSION';if(config.mode!==surface)config.page=0;config.mode=surface;}
 else if(surface==='DRUMS'||surface==='MELODY'){config.layer='DRUMS';config.melody=surface==='MELODY';}
 else config.layer=surface;
 return true;
}
export function createCabineNavigation({el,config,select}){
 const nav=el('nav','cabine-surfaces');nav.setAttribute('aria-label','Superfícies da CABINE');
 const active=cabineSurface(config);
 for(const surface of cabineSurfaces){const b=el('button',surface===active?'active':'',surface);b.type='button';b.dataset.cabineSurface=surface;b.setAttribute('aria-pressed',surface===active);b.onclick=()=>select(surface);nav.append(b);}
 return nav;
}
