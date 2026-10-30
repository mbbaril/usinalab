// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';

// Keep strings intact while masking comments, preserving source offsets.
export function sourceWithoutComments(source){
  return source.replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\/\/[^\n]*|\/\*[\s\S]*?\*\//g,
    token=>token.startsWith('//')||token.startsWith('/*')?token.replace(/[^\n]/g,' '):token);
}
export function braceBody(source,opening){
  assert.equal(source[opening],'{');
  const tokens=/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[{}]/g;tokens.lastIndex=opening;
  let depth=0;
  for(let m;(m=tokens.exec(source));){
    if(m[0]==='{')depth++;
    else if(m[0]==='}'&&--depth===0)return source.slice(opening+1,m.index);
  }
  assert.fail('Unterminated native brace block');
}
export function nativeFunction(source,signature){
  const clean=sourceWithoutComments(source),start=clean.indexOf(signature);
  assert(start>=0,`Missing native function ${signature}`);
  return braceBody(clean,clean.indexOf('{',start+signature.length));
}
export function nativeStrings(source,name){
  const clean=sourceWithoutComments(source),escaped=name.replace(/[.*+?^${}()|[\]\\]/g,'\\$&');
  const match=new RegExp(`\\b${escaped}\\b\\s*(?:\\[[^\\]]*\\])?\\s*(?:=\\s*)?\\{`).exec(clean);
  assert(match,`Missing native initializer ${name}`);
  return [...braceBody(clean,match.index+match[0].length-1).matchAll(/"(?:\\.|[^"\\])*"/g)].map(m=>JSON.parse(m[0]));
}
export function nativeKnobRows(source){
  return [...sourceWithoutComments(source).matchAll(/for\s*\(auto\s*\*\s*k\s*:\s*\{([^}]+)\}/g)]
    .map(m=>[...m[1].matchAll(/&([A-Za-z_]\w*)/g)].map(v=>v[1]));
}
export function nativeRow(rows,member){
  const found=rows.filter(row=>row.includes(member));
  assert.equal(found.length,1,`Expected one native knob row containing ${member}`);
  return found[0];
}
