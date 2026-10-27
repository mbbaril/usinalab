// SPDX-License-Identifier: GPL-3.0-only
// Action order, MIDI matching and pressure behavior from Cabine.h and PluginProcessor::processBlock.
export const cabineActions=['MODE ROTAS','MODE SONS','MODE GESTOS','PAGE <','PAGE >','PRESET <','PRESET >','INIT','FREEZE DELAY (held)','FREEZE VAPOR (held)','MACRO 1 (held)','MACRO 2 (held)','TAP LFO 1','TAP LFO 2','TAP LFO 3','TAP DELAY'];
export function actionBindings(data){return Array.from({length:16},(_,i)=>{const b=data?.[i];return b&&['cc','note'].includes(b.kind)&&Number.isInteger(b.number)&&b.number>=0&&b.number<128&&Number.isInteger(b.channel)&&b.channel>=-1&&b.channel<16?{kind:b.kind,number:b.number,channel:b.channel}:null;});}
export function createCabineActions({state,run,changed=()=>{}}){
  let learning=-1;const held=new Set();state.actionBindings=actionBindings(state.actionBindings);
  const matches=(binding,kind,number,channel)=>binding?.kind===kind&&binding.number===number&&(binding.channel===-1||binding.channel===channel);
  function release(index){if(held.delete(index))run(index,false,0,false);}
  return {
    get learning(){return learning;},
    learn(index){learning=learning===index?-1:index;changed(false);},
    stopLearn(){learning=-1;changed(false);},
    forget(index){release(index);state.actionBindings[index]=null;if(learning===index)learning=-1;changed(true);},
    cancel(){for(const index of [...held])release(index);},
    handle({status,a,b,timestamp=performance.now()}){
      const kind=status&240,channel=status&15;
      if(kind===160){for(const index of [10,11])if(matches(state.actionBindings[index],'note',a,channel)){held.add(index);run(index,true,b/127,true,timestamp);}return false;}
      if(![128,144,176].includes(kind))return false;
      const type=kind===176?'cc':'note',pressed=kind===176?b>0:kind===144&&b>0,value=pressed?b/127:0;
      if(learning>=0&&pressed){release(learning);state.actionBindings[learning]={kind:type,number:a,channel};learning=-1;changed(true);}
      let consumed=false;
      for(let index=0;index<16;++index)if(matches(state.actionBindings[index],type,a,channel)){
        if(index>=8&&index<=11){if(pressed)held.add(index);else held.delete(index);}run(index,pressed,value,false,timestamp);consumed||=type==='note';
      }
      return consumed;
    }
  };
}
