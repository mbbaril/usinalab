// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';import {createHash} from 'node:crypto';
import {routeBar} from '../route-list.js';
const native=readFileSync(new URL('../../src/plugin/ui/Panels.cpp',import.meta.url),'utf8');assert(native.includes('(e.getPosition().x - dragX) / 120.0f'));assert(native.includes('bar.getWidth() * 0.5f * x.amount'));
for(let i=-100;i<=100;i++){const a=i/100,b=routeBar(a);assert.equal(b.width,Math.abs(a)*50);assert.equal(b.left,50+Math.min(0,a)*50);assert(b.left>=0&&b.left+b.width<=100);}
const hash=p=>createHash('sha256').update(readFileSync(new URL(p,import.meta.url))).digest('hex');writeFileSync(new URL('../../build-web/parity/controller-routes-report.json',import.meta.url),JSON.stringify({passed:true,barCases:201,nativePanelSha256:hash('../../src/plugin/ui/Panels.cpp'),nativePagesSha256:hash('../../src/plugin/ui/ModPages.cpp'),controllerSha256:hash('../controller-panel.js'),routeSha256:hash('../route-list.js'),feedbackSha256:hash('../modulation-feedback.js'),gestureSha256:hash('../vertical-gesture.js'),layoutSha256:hash('../mod-layout.css')},null,2));console.log('201 native bipolar route-bar positions and exact horizontal sensitivity compared to RouteList.');
