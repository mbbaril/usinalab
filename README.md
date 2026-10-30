# Usina — alfa web

O sintetizador Usina no navegador: **https://lab.usina.ruido.art.br/**

Versão alfa. Funciona melhor em navegadores Chromium, no Android e no computador. Safari (iOS e
iPadOS) e MIDI físico ainda estão em validação.

## Uso

1. Abra o endereço e toque em **Ativar áudio**.
2. Salve a sessão antes de recarregar a página.

Ainda não disponível nesta alfa: resposta a impulso (IR), sincronia MIDI, troca direta de presets,
sensores, bancos e transição entre presets. Um arquivo `.usina` exportado contém só o timbre; o
plugin desativa a sequência ao carregá-lo.

## Fontes

Cada distribuição tem as suas fontes correspondentes; elas valem para a versão indicada.

| Distribuição | Fontes |
|---|---|
| Web 0.1.49-alpha.7 | [usinalab-source-0.1.49-alpha.7.zip](https://lab.usina.ruido.art.br/usinalab-source-0.1.49-alpha.7.zip), também em MENU → Créditos |
| Android 0.1.45 | [usina-android-source-0.1.45.zip](https://lab.usina.ruido.art.br/usina-android-source-0.1.45.zip): o build limpo reproduz o APK (SHA-256 `26e6517452bb0035bd8a644ff9369ac469c50fd958fc5df369d8059f6745cef8`); instruções em `ANDROID-BUILD.md`; chaves de assinatura não incluídas |

Interface e motor estão em `source/`; o build está descrito em `source/BUILD.md`.

## Licenças

O conjunto é distribuído sob a AGPL-3.0 (`LICENSE`). As partes GPL-3.0, MIT, Apache-2.0 e OFL mantêm
suas licenças; os avisos estão em `NOTICE.md` e acompanham `site/` e `source/`. O instrumento não
coleta dados nem envia telemetria.

## Contato

Problemas e sugestões: [Issues](https://github.com/mbbaril/usinalab/issues).

## English

The Usina synthesizer in the browser: **https://lab.usina.ruido.art.br/**. Alpha. Best on Chromium
(Android and desktop); Safari (iOS, iPadOS) and hardware MIDI are still being validated.

Press **Ativar áudio** (enable audio) to start, and save the session before reloading. Not yet in
this alpha: impulse responses, MIDI sync, direct preset switching, sensors, banks, preset morphing.
Exported `.usina` files hold the sound only.

Each release links its corresponding source archive (table above); build instructions are in
`source/BUILD.md`. The combined work is licensed under AGPL-3.0; components keep their own licences
(`NOTICE.md`). No data collection, no telemetry. Issues: this repository.

## Mudança de endereço / Address change

O synth está em https://lab.usina.ruido.art.br/ . Presets e sessões guardados no navegador pertencem ao endereço antigo: exporte-os no endereço antigo antes de migrar. O novo endereço começa com armazenamento vazio; arquivos exportados podem ser importados. O plugin e o manual ficam em https://usina.ruido.art.br/ .

Browser-saved presets and sessions belong to the old address. Export them there before moving; the new address starts with empty storage. Import your exported files at the new address.
