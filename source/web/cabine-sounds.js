// SPDX-License-Identifier: GPL-3.0-only
import {presetCategories,filterPresets,inPresetCategory,readFavourites} from './preset-browser.js';
export function renderCabineSounds({root,el,library,current,choose,preferences,normalized,setNormalized,color,storage,pulse=()=>{}}){
  try{storage??=globalThis.localStorage;}catch{}
  const panel=el('section','cabine-sounds'),head=el('div','cabine-sounds-head'),now=el('span','cabine-sounds-current'),search=el('input','cabine-sounds-search'),count=el('output','cabine-sounds-count'),body=el('div','cabine-sounds-body'),categories=el('nav','cabine-sounds-categories'),list=el('div','cabine-sounds-list'),footer=el('div','cabine-sounds-macros');
  panel.setAttribute('aria-label','SONS · biblioteca');categories.setAttribute('aria-label','Categorias de timbres');list.setAttribute('role','listbox');list.setAttribute('aria-label','Timbres disponíveis');search.type='search';search.placeholder='Buscar timbre';search.setAttribute('aria-label','Buscar timbre na CABINE');search.value=preferences.query;
  let favourites=new Set(),rows=new Map(),lastCurrent=null;const macroButtons=[];
  function readStars(){try{favourites=readFavourites(storage.getItem('usina.favourites.v1'),library);}catch{favourites=new Set();}}
  function refresh(){const name=current();if(lastCurrent!==name){lastCurrent=name;now.textContent=`TIMBRE · ${name}`;now.title=name;for(const[p,row]of rows){const selected=p.name===name;row.classList.toggle('active',selected);row.setAttribute('aria-selected',selected);}}
    macroButtons.forEach(({button,macro,step})=>button.setAttribute('aria-pressed',String(Math.round(normalized(`macro${macro+1}`)*4)>=step)));
  }
  function renderList(){
    rows.clear();const shown=filterPresets(library,preferences.category,preferences.query,favourites);count.textContent=`${shown.length} / ${library.length}`;
    list.replaceChildren(...shown.map(p=>{const button=el('button','cabine-sound-card'),caption=el('span','',presetCategories.find(([id])=>id===p.cat)?.[1]||p.cat),name=el('strong','',p.name),description=el('small','',p.desc?.pt||p.desc?.en||'');button.type='button';button.setAttribute('role','option');button.setAttribute('aria-label',p.name);button.title=p.desc?.pt||p.name;button.style.setProperty('--sound-color',color(p.cat));if(favourites.has(p.name))caption.append(el('span','cabine-sound-star',' ♥'));button.append(caption,name,description);button.onclick=()=>choose(p);rows.set(p,button);return button;}));
    if(!shown.length)list.append(el('p','cabine-sounds-empty',preferences.category==='favourites'?'Nenhum favorito nesta seleção.':'Nenhum timbre encontrado.'));
    lastCurrent=null;refresh();
  }
  function renderCategories(){categories.replaceChildren(...[['','Todos'],['favourites','Favoritos'],...presetCategories].map(([key,label])=>{const button=el('button',''),n=library.filter(p=>inPresetCategory(p,key,favourites)).length;button.type='button';button.dataset.soundCategory=key;button.setAttribute('aria-pressed',key===preferences.category);button.append(el('span','',label),el('small','',String(n)));button.onclick=()=>{preferences.category=key;readStars();renderCategories();renderList();list.scrollTop=0;};return button;}));}
  search.oninput=()=>{preferences.query=search.value;renderList();list.scrollTop=0;};search.onkeydown=e=>{if(e.key==='Escape'){e.preventDefault();search.value='';preferences.query='';renderList();search.blur();}else if(e.key==='Enter'){const first=filterPresets(library,preferences.category,preferences.query,favourites)[0];if(first){e.preventDefault();search.blur();choose(first);}}};
  for(let macro=0;macro<2;macro++){const row=el('div','cabine-sound-macro');row.append(el('span','',`MACRO ${macro+1}`));for(let step=1;step<=4;step++){const value=step/4,button=el('button','play-toggle',`${step*25}%`);button.setAttribute('aria-label',`Macro ${macro+1} ${step*25} por cento normalizado`);button.onclick=()=>{pulse();const id=`macro${macro+1}`;setNormalized(id,Math.abs(normalized(id)-value)<.01?0:value);refresh();};macroButtons.push({button,macro,step});row.append(button);}footer.append(row);}
  head.append(now,count,search);body.append(categories,list);panel.append(head,body,footer);root.append(panel);readStars();renderCategories();renderList();return{refresh,cancel(){if(search===globalThis.document?.activeElement)search.blur();}};
}
