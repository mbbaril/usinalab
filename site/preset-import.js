// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Mario Baril
// Read the actual JUCE saveUserPreset XML. This imports a sound, never guesses at a whole host session.
const RIG = new Set(['in_gain', 'in_mix', 'in_fx', 'feedback', 'fb_time', 'level', 'ceiling', 'oversampling', 'seq_bpm', 'seq_play', 'bend_range', 'mpe_mode', 'mpe_bend_range']);
const IGNORED_PARAMETERS = new Set(['seq_on', 'ir_on', 'ir_mix', 'ir_lowcut', 'ir_highcut', 'ir_out']);
const IGNORED_CHILDREN = new Set(['SEQ', 'CTRLS', 'CCMAP', 'CABACT', 'LAUNCHPAD', 'CABINE']);
const DEFAULT_FUNCTIONS = [[[0, 1, -.6], [1, 0, 0]], [[0, 0, 0], [.5, 1, 0], [1, 0, 0]], [[0, 0, .3], [1, 1, 0]]];
const elements = node => Array.from(node.childNodes || []).filter(n => n.nodeType === 1);
const cloneFunctions = () => DEFAULT_FUNCTIONS.map(f => f.map(p => [...p]));

export function parseVstSound(text, metadata, Parser = globalThis.DOMParser) {
  if (typeof text !== 'string' || text.length > 2 * 1024 * 1024) throw Error('Preset inválido ou maior que 2 MB.');
  if (/<!DOCTYPE|<!ENTITY|<\?(?!xml(?:\s|\?>))/i.test(text)) throw Error('Declarações externas e entidades não são aceitas no preset.');
  if (!Parser) throw Error('Leitor XML indisponível neste ambiente.');
  const doc = new Parser().parseFromString(text, 'application/xml');
  if (!doc?.documentElement || doc.getElementsByTagName('parsererror').length) throw Error('XML inválido. Exporte um preset .usina pelo Usina desktop.');
  const root = doc.documentElement;
  if (root.tagName !== 'USINA' || root.namespaceURI || root.getAttribute('usinaPreset') !== '1') throw Error('Este arquivo não é um preset de som .usina. Projetos e arquivos .vstpreset não são aceitos.');
  const version = Number(root.getAttribute('stateVersion'));
  if (!Number.isInteger(version) || version < 3 || version > 6) throw Error('Versão de preset não suportada. Exporte novamente pelo Usina desktop atual.');
  const children = elements(root);
  if (children.length > 2000) throw Error('Preset com elementos demais.');
  const byId = new Map(metadata.parameters.map(d => [d.id, d]));
  const sources = new Map(metadata.sources.map(s => [s.id, s.index]));
  const destinations = new Map(metadata.destinations.map(d => [d.name, d.index]));
  const plan = { name: (root.getAttribute('userPreset') || 'SOM IMPORTADO').slice(0, 120), params: {}, routes: [], functions: cloneFunctions(), blockers: [], notices: [], version };
  const seen = new Set(), sections = new Set();
  const blocked = message => { if (plan.blockers.length < 50) plan.blockers.push(message); };
  const number = (node, attr) => { const raw = node.getAttribute(attr); if (raw === null || !raw.trim() || !Number.isFinite(Number(raw))) throw Error(`Valor inválido em ${node.tagName}.${attr}.`); return Number(raw); };
  for (const child of children) {
    if (child.tagName === 'PARAM') {
      const id = child.getAttribute('id');
      if (!id || seen.has(id)) throw Error('Parâmetro ausente ou duplicado no preset.');
      seen.add(id);
      const value = number(child, 'value');
      if (RIG.has(id) || IGNORED_PARAMETERS.has(id)) continue;
      const d = byId.get(id);
      if (!d) { blocked(`Parâmetro não suportado: ${id.slice(0, 80)}.`); continue; }
      const epsilon = 1e-6 * Math.max(1, Math.abs(d.min), Math.abs(d.max));
      if (value < d.min - epsilon || value > d.max + epsilon) { blocked(`Valor fora da faixa: ${d.name}.`); continue; }
      if (d.step === 1 && Math.abs(value - Math.round(value)) > 1e-4) { blocked(`Valor inteiro inválido: ${d.name}.`); continue; }
      plan.params[id] = Math.min(d.max, Math.max(d.min, d.min + Math.round((value - d.min) / d.step) * d.step));
    } else {
      if (sections.has(child.tagName)) throw Error(`Seção duplicada: ${child.tagName}.`);
      sections.add(child.tagName);
      if (IGNORED_CHILDREN.has(child.tagName)) continue;
      if (child.tagName === 'MODS') {
        const rows = elements(child);
        if (rows.length > 64) { blocked('O preset tem mais de 64 rotas de modulação.'); continue; }
        for (const row of rows) {
          if (row.tagName !== 'ROUTE') { blocked('Elemento de modulação desconhecido.'); continue; }
          const source = sources.get(row.getAttribute('src')), name = row.getAttribute('dst'), destination = destinations.get(name), amount = number(row, 'amount');
          if (source === undefined || destination === undefined || name?.startsWith('IR ') || amount < -1 || amount > 1) { blocked(`Rota não suportada: ${row.getAttribute('src')} → ${name}.`); continue; }
          plan.routes.push({ source, destination, amount });
        }
      } else if (child.tagName === 'FUNCS') {
        const funcs = elements(child);
        if (funcs.length > 3) { blocked('O preset tem mais de três funções.'); continue; }
        funcs.forEach((func, index) => {
          if (func.tagName !== 'FUNC') { blocked('Elemento de função desconhecido.'); return; }
          const points = (func.getAttribute('points') || '').split(';').map(p => p.trim().split(/\s+/).map(Number));
          if (points.length < 2 || points.length > 64 || points.some((p, i) => p.length !== 3 || p.some(v => !Number.isFinite(v)) || p[0] < 0 || p[0] > 1 || p[1] < 0 || p[1] > 1 || Math.abs(p[2]) > 1 || i > 0 && p[0] < points[i - 1][0])) { blocked(`Curva inválida em FUNC ${index + 1}.`); return; }
          plan.functions[index] = points;
        });
      } else blocked(`Seção de som não suportada: ${child.tagName.slice(0, 80)}.`);
    }
  }
  if (!Object.keys(plan.params).length) blocked('Nenhum parâmetro de som reconhecido.');
  const valueOf = id => { const item = children.find(n => n.tagName === 'PARAM' && n.getAttribute('id') === id); return item ? number(item, 'value') : undefined; };
  const irPlaced = Object.entries(plan.params).some(([id, value]) => /^(fx_[ab][23]?|aux[23]?|loop[23]?)_type$/.test(id) && value === 4);
  if (irPlaced && (valueOf('ir_on') ?? 1) > .5) blocked('O som usa convolução IR em um slot. Essa função ainda não está portada.');
  if (irPlaced && (valueOf('ir_on') ?? 1) <= .5) plan.notices.push('Há um slot IR desligado; ele permanecerá sem processamento.');
  if (sections.has('SEQ')) plan.notices.push('A sequência do arquivo não será importada; suas pistas atuais serão preservadas e paradas.');
  if ([...sections].some(s => ['CTRLS', 'CCMAP', 'CABACT', 'LAUNCHPAD', 'CABINE'].includes(s))) plan.notices.push('Mapas MIDI e configurações de performance do arquivo não serão importados.');
  plan.notices.push('Volume, entrada, qualidade e mapas MIDI atuais serão preservados. A realimentação volta a zero.');
  return plan;
}

export function applyVstSound(current, plan, metadata) {
  if (!plan || plan.blockers.length) throw Error('O preset contém funções não suportadas e não pode ser aplicado.');
  const params = { ...current.params };
  for (const d of metadata.parameters) if (!RIG.has(d.id)) params[d.id] = d.default;
  Object.assign(params, plan.params, { seq_play: 0, feedback: 0 });
  return { ...current, params, preset: plan.name, routes: plan.routes.map(r => ({ ...r })), functions: plan.functions.map(f => f.map(p => [...p])) };
}
