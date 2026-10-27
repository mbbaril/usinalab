// SPDX-License-Identifier: GPL-3.0-only
// Read-only UI telemetry. Fixed storage, at most one unacknowledged packet, independent of scopes.
export class VisualSourcePublisher{
  constructor(post,read){this.post=post;this.read=read;this.ids=new Uint8Array(128);this.count=0;this.frames=0;this.busy=false;this.packet={type:'modSources',token:0,count:0,time:0,values:new Float32Array(256)};}
  configure(ids,token){this.count=0;this.frames=0;this.busy=false;this.packet.token=token;
    for(const id of ids||[]){if(!Number.isInteger(id)||id<=0||id>=128||this.count>=128)continue;let duplicate=false;for(let i=0;i<this.count;i++)if(this.ids[i]===id)duplicate=true;if(!duplicate)this.ids[this.count++]=id;}
  }
  ack(token){if(token===this.packet.token)this.busy=false;}
  tick(frames,time,rate){if(!this.count)return;this.frames+=frames;if(this.frames<rate/30)return;this.frames%=rate/30;if(this.busy)return;
    for(let i=0;i<this.count;i++){this.packet.values[2*i]=this.ids[i];const value=this.read(this.ids[i]);this.packet.values[2*i+1]=Number.isFinite(value)?value:0;}
    this.busy=true;this.packet.count=this.count;this.packet.time=time;this.post(this.packet);
  }
}
