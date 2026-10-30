// SPDX-License-Identifier: GPL-3.0-only
// Run the native ProcessorTest --export-fx-ui first, then this generator.
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
const file=p=>new URL(p,import.meta.url),hash=p=>createHash('sha256').update(readFileSync(file(p))).digest('hex');
const ids=['tex_size','tex_pitch','tex_time_amt','tex_size_amt','tex_shape_amt','tex_pitch_amt','tex_density','tex_gain','vap_pitch','fxf_drive'];
const tables=Object.fromEntries(ids.map(id=>[id,Array(1001).fill(null)]));
for(const line of readFileSync(file('../build-web/parity/fx-processor-ui.tsv'),'utf8').trim().split(/\r?\n/)) {
  const[id,value,text]=line.split('\t');if(tables[id])tables[id][Math.round(+value*1000)]=text;
}
if(!Object.values(tables).every(a=>a.every(v=>typeof v==='string')))throw Error('Incomplete native knob positions');
writeFileSync(file('./fx-native-text.js'),'// SPDX-License-Identifier: GPL-3.0-only\n// Generated from JUCE parameter text at every legal 0.001 knob position.\nexport default '+JSON.stringify(tables)+';\n');
writeFileSync(file('./fx-native-text.provenance.json'),JSON.stringify({processorSha256:hash('../src/plugin/PluginProcessor.cpp'),vaporSha256:hash('../src/fx/Vapor.cpp'),fixtureSha256:hash('../build-web/parity/fx-processor-ui.tsv'),tableSha256:hash('./fx-native-text.js')},null,2));
