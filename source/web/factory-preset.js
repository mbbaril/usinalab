// SPDX-License-Identifier: GPL-3.0-only
// UsinaProcessor::selectPreset changes the patch parameters; performance controls stay as they are.
export function factoryParameters(current,preset,metadata){
 const params={...current},byName=new Map(metadata.parameters.map(d=>[d.name,d]));
 for(let i=0;i<metadata.patchCount;i++){const d=metadata.parameters[i];params[d.id]=d.default;}
 for(const[name,value]of Object.entries(preset.values||{})){const d=byName.get(name);if(d)params[d.id]=Math.min(d.max,Math.max(d.min,value));}
 return params;
}
