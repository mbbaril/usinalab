// SPDX-License-Identifier: GPL-3.0-only
import createAnalyzer from './filter-analyzer.mjs';
const ready=createAnalyzer();
self.onmessage=async({data})=>{
  try{const m=await ready,p=data.params,status=m._filter_measure(...p),values=status?null:Array.from(m.HEAPF32.subarray(m._filter_result()/4,m._filter_result()/4+321));self.postMessage({id:data.id,status,values});}
  catch{self.postMessage({id:data.id,status:2,values:null});}
};
