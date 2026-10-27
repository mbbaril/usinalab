// SPDX-License-Identifier: GPL-3.0-only
export function createSequenceGenerator({state,send,message,changed}){
  let worker=null,pending=null,next=0,seed=1,timeout=null;
  const snapshot=lane=>JSON.stringify([state.lanes[lane],state.sequence[lane]]);
  const done=()=>{clearTimeout(timeout);pending=null;changed();};
  function run(lane,operation,options,commit=action=>action()){if(pending)return;
    const id=++next;pending={id,lane,sequence:state.sequence,snapshot:snapshot(lane),commit};timeout=setTimeout(()=>{message('O gerador não respondeu. Tente novamente.',true);worker?.terminate();worker=null;done();},15000);changed();
    try{
      if(!worker){worker=new Worker(new URL('./sequence-worker.js',import.meta.url),{type:'module'});worker.onmessage=({data})=>{
        if(data.id!==pending?.id)return;const request=pending;
        if(data.error){message(data.error,true);done();return;}
        if(state.sequence!==request.sequence||snapshot(request.lane)!==request.snapshot){message('A pista mudou durante a geração. O resultado não foi aplicado.');done();return;}
        request.commit(()=>{state.sequence[request.lane]=data.steps;send({type:'part',lane:request.lane,settings:state.lanes[request.lane],steps:data.steps});});done();
      };worker.onerror=()=>{message('Não foi possível executar o gerador.',true);worker.terminate();worker=null;done();};}
      worker.postMessage({id,lane,operation,...options,seed,state:{sequence:state.sequence,lanes:state.lanes}});if(operation===1||operation===2)seed=(seed+1)>>>0;
    }catch(error){message(error.message,true);done();}
  }
  return {run,get busy(){return !!pending;}};
}
