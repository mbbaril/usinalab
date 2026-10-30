#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mario Brandalise Baril
set -euo pipefail
cd "$(dirname "$0")/.."
if ! command -v emcmake >/dev/null 2>&1; then
    . "${EMSDK:-$HOME/emsdk}/emsdk_env.sh"
fi
emcmake cmake -S web -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --target usina filter_analyzer -j 4
mkdir -p build-web/dist
cp web/native-audio-protocol.js web/native-audio-transport.js web/native-audio-node.js build-web/dist/
rm -f build-web/dist/THIRD_PARTY.md
node web/export-metadata.mjs
node web/generate-algorithms.mjs
cp web/cabine-navigation.js web/preset-export.js web/preset-export-ui.js web/filter-layout.css web/fx-choices.js web/texture-layout.js web/texture-layout.css web/fx-bus-layout.js web/preset-browser.js web/preset-browser.css web/factory-preset.js web/controller-panel.js web/route-list.js web/mod-layout.js web/mod-layout.css web/response-curve.js web/operator-overview.js web/synth-layout.css web/typing-keyboard.js web/roll-notes.js web/roll-history.js web/note-delete-gesture.js web/modulation-feedback.js web/visual-sources.js web/filter-layout.js web/vertical-gesture.js web/synth-panel.js web/synth-visuals.js web/mod-panel.js web/function-editor.js web/synth.css web/algorithms.json build-web/dist/
cp build-web/filter-analyzer.mjs build-web/filter-analyzer.wasm web/filter-response.js web/filter-response-worker.js web/operator-layout.js web/synth-parameters.js web/fx-editor-layout.js web/envelope-link.js web/tap-control.js web/lfo-actions.js web/fx-values.js web/fx-native-text.js web/fx-slot-drag.js web/graph-viewport.js web/fx-panel.js web/scope-viewport.js web/matrix-viewport.js web/note-duration.js web/piano-roll.js web/sequence-harmony.js web/sequence-worker.js web/sequence-generator.js web/sequence-wire.js web/sequence.css web/sequence-model.js web/sequence-panel.js web/touch-value.js web/source-scope.js web/keybed-state.js web/local-keyboard.js web/keybed.css build-web/dist/
cp build-web/usina.mjs build-web/usina.wasm web/index.html web/style.css web/instrument.css web/app.js web/controls.js web/performance.js web/cabine.js web/cabine-keys.js web/cabine-user.js web/cabine-sequence.js web/cabine-actions.js web/mapping-files.js web/mapping-panel.js web/haptics.js web/modulation-tray.js web/modulation-sources.js web/cabine-gestures.js web/instrument-shell.js web/credits.js web/midi-pickup.js web/midi-learn-ui.js web/mpe.js web/midi-output.js web/mpe-controller.js web/play-model.js web/play-surface.js web/play.css web/cabine-remote.js web/cabine-remote.css web/sample-panel.js web/sample-panel.css web/worklet.js web/midi.js web/midi-parser.js web/preset-import.js build-web/dist/
cp resources/presets/factory_library.json build-web/dist/library.json
cp LICENSE LICENSE.AGPL-3.0 web/THIRD_PARTY-NOTICES.txt web/Apache-LICENSE.txt web/Capacitor-LICENSE.txt build-web/dist/
cp -r web/fonts build-web/dist/
cp web/manifest.webmanifest web/icon.svg build-web/dist/
cp web/cabine-touch.js web/cabine-touch.css web/play-feedback.js web/cabine-sounds.js web/cabine-sounds.css build-web/dist/
cp web/popup-keyboard.js web/performance-keyboard.js web/fx-slot-actions.js web/accessible-controls.js web/accessibility.css build-web/dist/
BUILD_ID=$(sha256sum build-web/usina.wasm web/*.js web/*.html web/*.css web/*.webmanifest web/icon.svg web/fonts/* | sha256sum | cut -c1-12)
sed "s/@BUILD_ID@/$BUILD_ID/g" web/service-worker.js > build-web/dist/service-worker.js
cp web/README.md build-web/dist/README.md
echo 'Usina Web: build-web/dist (serve over localhost or HTTPS)'
