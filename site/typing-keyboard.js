// SPDX-License-Identifier: GPL-3.0-only
export const typingPitches={KeyA:0,KeyW:1,KeyS:2,KeyE:3,KeyD:4,KeyF:5,KeyT:6,KeyG:7,KeyY:8,KeyH:9,KeyU:10,KeyJ:11,KeyK:12};
export function typingCode(event){if(event.code&&event.code!=='Unidentified')return event.code;const letter=event.key?.toUpperCase();return letter?.length===1?`Key${letter}`:'';}
export function acceptsMusicalTyping(event){
  if(event.defaultPrevented||event.isComposing||event.ctrlKey||event.metaKey||event.altKey||event.repeat||typingPitches[typingCode(event)]===undefined)return false;
  const node=event.target;if(node?.isContentEditable||['SELECT','TEXTAREA'].includes(node?.tagName))return false;
  return node?.tagName!=='INPUT'||['range','checkbox','radio','button','submit','reset'].includes(node.type);
}
export function installTypingKeyboard({target,base,press,release,escape=()=>{}}){
  const down=e=>{if(e.code==='Escape'||e.key==='Escape'){escape();return;}if(!acceptsMusicalTyping(e))return;e.preventDefault();const code=typingCode(e),pitch=base()+typingPitches[code];if(Number.isInteger(pitch)&&pitch>=0&&pitch<=127)press(code,pitch);};
  // Release belongs to the key that started the note, regardless of the current focus or octave.
  const up=e=>{const code=typingCode(e);if(typingPitches[code]!==undefined)release(code);};
  target.addEventListener('keydown',down);target.addEventListener('keyup',up);
  return ()=>{target.removeEventListener('keydown',down);target.removeEventListener('keyup',up);};
}
export function releasePointerChoiceFocus(select){
  let pointer=false;select.addEventListener('pointerdown',()=>pointer=true);select.addEventListener('keydown',()=>pointer=false);
  select.addEventListener('change',()=>{if(pointer){pointer=false;select.blur();}});
}
