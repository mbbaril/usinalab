// SPDX-License-Identifier: GPL-3.0-only
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=new URL('../',import.meta.url);
const sources=['src/engines/fm/FmEngine.cpp','src/engines/fm/FmTables.cpp','src/engines/fm/FmTables.h'];
const sourceHash=()=>createHash('sha256').update(Buffer.concat(sources.map(p=>readFileSync(new URL(p,root))))).digest('hex');
const target=new URL('algorithms.json',import.meta.url);
if(process.argv.includes('--generate')){
  const algorithms=JSON.parse(execFileSync(fileURLToPath(new URL('build-web-native/usina_web_algorithms',root)),{encoding:'utf8'}));
  writeFileSync(target,JSON.stringify({sourceSha256:sourceHash(),algorithms}));
}
const data=JSON.parse(readFileSync(target,'utf8'));
if(data.sourceSha256!==sourceHash()||data.algorithms.length!==40)throw Error('Regenerate algorithm diagrams from the native engine.');
console.log('40 native algorithm diagrams: source fingerprint verified');
