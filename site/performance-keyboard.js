// SPDX-License-Identifier: GPL-3.0-only
const activation=key=>key==='Enter'||key===' ';
export function installReleaseGuard(release,win=window,doc=document){
  const hidden=()=>{if(doc.hidden)release();},escape=e=>{if(e.key==='Escape')release();};
  win.addEventListener('blur',release);win.addEventListener('keydown',escape);doc.addEventListener('visibilitychange',hidden);
  return ()=>{win.removeEventListener('blur',release);win.removeEventListener('keydown',escape);doc.removeEventListener('visibilitychange',hidden);};
}
export function installPerformanceKeys(items){
  items=[...items].sort((a,b)=>a.row-b.row||a.col-b.col);
  const held=new Map(),cleanup=[];
  const release=item=>{if(held.delete(item))item.release();};
  const cancel=()=>{for(const item of [...held.keys()])release(item);};
  items.forEach((item,index)=>{
    const {target}=item;target.tabIndex=index===0?0:-1;target.setAttribute('role','button');
    const focus=()=>items.forEach(other=>other.target.tabIndex=other===item?0:-1);
    const down=e=>{
      if(activation(e.key)){e.preventDefault();e.stopPropagation();if(!e.repeat&&!held.has(item)){held.set(item,true);item.press();}return;}
      if(e.key==='Escape'){e.preventDefault();cancel();return;}
      if(!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home','End'].includes(e.key))return;
      e.preventDefault();e.stopPropagation();
      const vertical=e.key==='ArrowUp'||e.key==='ArrowDown',axis=vertical?'row':'col',same=vertical?'col':'row',sign=['ArrowUp','ArrowLeft'].includes(e.key)?-1:1;
      const next=e.key==='Home'?items[0]:e.key==='End'?items.at(-1):items.filter(other=>other[same]===item[same]&&(other[axis]-item[axis])*sign>0).sort((a,b)=>Math.abs(a[axis]-item[axis])-Math.abs(b[axis]-item[axis]))[0];
      if(next&&next!==item){release(item);next.target.focus();}
    };
    const up=e=>{if(activation(e.key)){e.preventDefault();e.stopPropagation();release(item);}};
    const blur=()=>release(item);
    for(const [event,fn]of [['focus',focus],['keydown',down],['keyup',up],['blur',blur]]){target.addEventListener(event,fn);cleanup.push(()=>target.removeEventListener(event,fn));}
  });
  return {release:cancel,dispose(){cancel();cleanup.forEach(fn=>fn());}};
}
