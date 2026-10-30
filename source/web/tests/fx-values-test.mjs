// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {fxValueFormatter,textureGroups} from '../fx-values.js';
const lines=readFileSync(new URL('../../build-web/parity/fx-processor-ui.tsv',import.meta.url),'utf8').trim().split(/\r?\n/);
let checks=0;const errors=[];
for(const line of lines){const[id,value,text]=line.split('\t'),format=fxValueFormatter(id);assert(format,id);const unit=['dly_out','dist_drive','dist_out','aux_return','fxf_out'].includes(id)?' dB':'',actual=format(+value);checks++;if(actual!==text+unit)errors.push({id,value,expected:text+unit,actual});}
assert.equal(checks,36036);assert.deepEqual(errors.slice(0,12),[],`${errors.length} mismatched native strings`);
const nativePanel=readFileSync(new URL('../../src/plugin/ui/Panels.cpp',import.meta.url),'utf8').split('TexturePanel::TexturePanel')[1];for(const[,ids]of textureGroups)for(const id of ids)assert(nativePanel.includes(`"${id}"`));assert.equal(new Set(textureGroups.flatMap(g=>g[1])).size,18);
const hash=p=>createHash('sha256').update(readFileSync(new URL(p,import.meta.url))).digest('hex');
const report={passed:true,checks,nativeSourceSha256:hash('../../src/plugin/PluginProcessor.cpp'),vaporSourceSha256:hash('../../src/fx/Vapor.cpp'),formatterSha256:hash('../fx-values.js'),tableSha256:hash('../fx-native-text.js')};
const provenance=JSON.parse(readFileSync(new URL('../fx-native-text.provenance.json',import.meta.url),'utf8'));assert.equal(provenance.processorSha256,report.nativeSourceSha256);assert.equal(provenance.vaporSha256,report.vaporSourceSha256);assert.equal(provenance.tableSha256,report.tableSha256);assert.equal(provenance.fixtureSha256,hash('../../build-web/parity/fx-processor-ui.tsv'));
writeFileSync(new URL('../../build-web/parity/fx-values-report.json',import.meta.url),JSON.stringify(report,null,2));
console.log(`${checks} actual JUCE FX readouts match; TEXTURE's four native control groups preserved.`);
