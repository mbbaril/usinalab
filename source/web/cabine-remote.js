// SPDX-License-Identifier: GPL-3.0-only
// Remote MIDI performance surface; the plugin owns its behavior.
// Channel 16 sends pad notes, button CCs, surface CC100..106 and refresh CC110.
// LED RGB values arrive on channels 14..16; CC102 reports the active surface.
// Shared protocol fixture: tests/fixtures/cabine-remote.json.
import {installPerformanceKeys,installReleaseGuard} from './performance-keyboard.js';
import {MidiOutput} from './midi-output.js';
export const REMOTE={channel:16,rgb:[14,15,16],surfaceCc:100,allCc:110,shownCc:102};
export const SURFACES=['ROTAS','SONS','GESTOS','DRUMS','MELODY','KEYS','USER'];
export const placeOf=(row,col)=>(8-row)*10+col+1;          // row 0 the top, col 0 the left
export const TOP=[91,92,93,94,95,96,97,98],SIDE=[89,79,69,59,49,39,29,19];   // the top row; the right column, top down
const st=kind=>kind|(REMOTE.channel-1);
export const remote={
  pad:(row,col,down)=>down?[st(0x90),placeOf(row,col),127]:[st(0x80),placeOf(row,col),0],
  button:(cc,down)=>[st(0xb0),cc,down?127:0],
  surface:k=>[st(0xb0),REMOTE.surfaceCc+k,127],
  hello:()=>[st(0xb0),REMOTE.allCc,127],
};
// what the plugin says: the places' colours and the surface; anything else is not this protocol's (false)
export class RemoteGrid{
  constructor(){this.colours=new Map();this.surface=-1;}
  receive(status,a,b){
    if((status&0xf0)!==0xb0)return false;const ch=(status&15)+1;
    if(ch===REMOTE.channel&&a===REMOTE.shownCc){this.surface=b;return true;}
    const i=REMOTE.rgb.indexOf(ch);if(i<0||a<11||a>99)return false;
    const c=this.colours.get(a)||[0,0,0];c[i]=b;this.colours.set(a,c);return true;
  }
  colour(place){return this.colours.get(place)||[0,0,0];}
  css(place){const[r,g,b]=this.colour(place);return `rgb(${r*2},${g*2},${b*2})`;}
}
export function createCabineRemote({el,status,listen}){
  const grid=new RemoteGrid(),output=new MidiOutput((text)=>{if(!output.ready)releaseAll();if(text)status(text);paintHead();});
  let root=null,head=null,frame=0,active=false;const held=new Map();let keyboard=null,unguard=null;
  const send=m=>{if(output.ready)output.send([m]).catch(e=>{if(!e.canceled)status(e.message,true);});};
  function receive(status,a,b){if(!active||!grid.receive(status,a,b))return false;if(!frame&&root)frame=requestAnimationFrame(paint);return true;}
  function paint(){frame=0;if(!root)return;root.querySelectorAll('[data-place]').forEach(e=>{e.style.background=grid.css(+e.dataset.place);});
    root.querySelectorAll('[data-surface]').forEach(b=>b.setAttribute('aria-pressed',String(+b.dataset.surface===grid.surface)));}
  function paintHead(){
    if(!head)return;const sel=head.querySelector('select'),cur=sel.value||output.selected?.key||'',sig=JSON.stringify(output.destinations.map(d=>[d.key,d.name]));
    if(sel.dataset.sig!==sig){sel.replaceChildren(el('option','','Escolha a porta do computador'));sel.options[0].value='';for(const d of output.destinations){const o=el('option','',d.name);o.value=d.key;sel.append(o);}sel.value=cur;sel.dataset.sig=sig;}
    head.querySelector('.remote-link').textContent=output.ready?`MIDI · ${output.selected?.name}`:'Conectar ao plugin';
    root?.classList.toggle('remote-connected',output.ready);
    const empty=root?.querySelector('.remote-empty');if(empty)empty.hidden=output.ready;
  }
  async function connect(key){
    releaseAll();try{await output.select(key);listen();active=true;send(remote.hello());status('CABINE REMOTA ligada. No plugin: CABINE → REMOTE, as portas do celular.');}
    catch(e){if(!e.canceled)status(e.message,true);}paintHead();
  }
  function press(id,down,up){if(held.has(id))return;const shared=[...held.values()].some(m=>m.toString()===up.toString());held.set(id,up);if(!shared)send(down);}
  function release(id){const m=held.get(id);if(!m)return;held.delete(id);if(![...held.values()].some(other=>other.toString()===m.toString()))send(m);}
  function releaseAll(){keyboard?.release();for(const id of [...held.keys()])release(id);}
  function render(target){
    releaseAll();keyboard?.dispose();unguard?.();root=target;root.classList.add('cabine-remote');
    const controls=el('aside','remote-console'),existingSwitch=root.querySelector('.remote-switch');if(existingSwitch)controls.append(existingSwitch);
    head=el('details','remote-head');const summary=el('summary','play-connection'),ports=el('div','remote-port-panel'),find=el('button','small-button','Buscar portas'),sel=el('select'),close=el('button','small-button','Desconectar'),link=el('span','remote-link');
    sel.setAttribute('aria-label','Porta MIDI do computador');
    find.onclick=async()=>{try{await output.connect();listen();status(output.destinations.length?'Escolha a porta do computador.':'Nenhuma porta. Ligue o celular ao computador por USB e escolha MIDI na notificação de USB do Android.');}catch(e){status(e.message,true);}paintHead();};
    sel.onchange=()=>{if(sel.value)connect(sel.value);};
    close.onclick=async()=>{releaseAll();active=false;try{await output.disconnect();}catch(e){status(e.message,true);}paintHead();};
    summary.append(link);ports.append(sel,find,close);head.append(summary,ports);controls.append(head,el('span','play-group-label','SUPERFÍCIE'));
    const surfaces=el('div','remote-surfaces');SURFACES.forEach((name,k)=>{const b=el('button','play-toggle',name);b.dataset.surface=k;b.onclick=()=>{releaseAll();send(remote.surface(k));};surfaces.append(b);});controls.append(surfaces);
    controls.append(el('p','remote-guide','Setas navegam; Espaço ou Enter mantém o pad pressionado até soltar. Escape solta. O plugin devolve as cores.'));
    // the 9 x 9: the top row's buttons, then each row's 8 pads and its right button
    const board=el('div','remote-board'),keyItems=[];board.setAttribute('role','group');board.setAttribute('aria-label','Grade remota. Setas navegam; Espaço ou Enter mantém pressionado; Escape solta.');
    const touch=(e,id,down,up)=>{e.preventDefault();e.currentTarget.setPointerCapture?.(e.pointerId);press(`${id}:pointer:${e.pointerId}`,down,up);};
    const lift=(e,id)=>{release(`${id}:pointer:${e.pointerId}`);};
    const button=(cc,cls)=>{const b=el('div',`remote-button ${cls}`);b.dataset.place=cc;b.setAttribute('aria-label',`Botão ${cls==='top'?'superior':'lateral'} ${cls==='top'?TOP.indexOf(cc)+1:SIDE.indexOf(cc)+1}`);const id=`b${cc}`;
      b.onpointerdown=e=>touch(e,id,remote.button(cc,true),remote.button(cc,false));b.onpointerup=b.onpointercancel=b.onlostpointercapture=e=>lift(e,id);keyItems.push({target:b,row:cls==='top'?0:SIDE.indexOf(cc)+1,col:cls==='top'?TOP.indexOf(cc):8,press:()=>press(`${id}:key`,remote.button(cc,true),remote.button(cc,false)),release:()=>release(`${id}:key`)});return b;};
    TOP.forEach(cc=>board.append(button(cc,'top')));board.append(el('span','remote-corner'));
    for(let row=0;row<8;++row){
      for(let col=0;col<8;++col){const p=el('div','remote-pad'),place=placeOf(row,col),id=`p${place}`;p.dataset.place=place;p.setAttribute('aria-label',`Pad ${row+1}, ${col+1}`);
        p.onpointerdown=e=>touch(e,id,remote.pad(row,col,true),remote.pad(row,col,false));p.onpointerup=p.onpointercancel=p.onlostpointercapture=e=>lift(e,id);keyItems.push({target:p,row:row+1,col,press:()=>press(`${id}:key`,remote.pad(row,col,true),remote.pad(row,col,false)),release:()=>release(`${id}:key`)});board.append(p);}
      board.append(button(SIDE[row],'side'));
    }
    const stage=el('div','remote-stage'),empty=el('div','remote-empty'),connectButton=el('button','play-toggle','Escolher porta MIDI');
    connectButton.onclick=()=>{head.open=true;sel.focus();};empty.append(el('strong','','Sua CABINE no plugin'),el('p','','Conecte a saída MIDI e habilite REMOTE na CABINE do plugin.'),connectButton);stage.append(board,empty);
    root.append(controls,stage);keyboard=installPerformanceKeys(keyItems);unguard=installReleaseGuard(releaseAll);paintHead();paint();
    if(output.ready){active=true;send(remote.hello());}
  }
  function leave(){releaseAll();keyboard?.dispose();keyboard=null;unguard?.();unguard=null;root?.classList.remove('cabine-remote');root=null;head=null;}
  return {render,leave,receive,releaseAll,active:()=>active};
}
