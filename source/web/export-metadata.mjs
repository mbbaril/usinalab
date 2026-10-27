// SPDX-License-Identifier: GPL-3.0-only
import create from '../build-web/usina.mjs';
import {writeFileSync} from 'node:fs';
const wasm=await create();writeFileSync(new URL('../build-web/dist/metadata.json',import.meta.url),wasm.UTF8ToString(wasm._web_schema()));
