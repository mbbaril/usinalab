# usinalab — fontes da alfa 0.1.45-alpha.2

Requisitos: Emscripten 4.0.23, CMake 3.28 ou superior e Node.js. O SDK fornece Node após carregar emsdk_env.sh. Execute `bash web/build.sh` na raiz deste pacote. A saída é build-web/dist; sirva por localhost ou HTTPS. Para regenerar os diagramas após alterar os algoritmos: cmake -S web -B build-web-native -DCMAKE_BUILD_TYPE=Release; cmake --build build-web-native --target usina_web_algorithms; node web/generate-algorithms.mjs --generate.

Inclui código próprio de interface, adaptador e DSP, fontes das dependências compiladas, dados, fontes tipográficas, avisos e entradas/scripts de build. Não inclui histórico Git, credenciais, conversas, planos internos ou projeto Android. O arquivo src/plugin/PluginProcessor.cpp e a fixture fx-processor-ui.tsv documentam a origem da tabela de captions; o build web não requer JUCE. A tabela fx-native-text.js é editável; generate-fx-text.mjs pode regenerá-la a partir da fixture fornecida. Os testes C++ Modal/Seq fazem parte da configuração CMake.

Esta é uma distribuição web; o plugin e o APK são artefatos diferentes. Créditos/licenças no MENU. MIDI no Safari/iPad e hardware físico não foram validados. As modificações de publicação são título/nome usinalab, link de fontes nos créditos e correção do link de avisos do footer. DSP e leis de controle permanecem iguais à revisão 0.1.45.

Bancos FM de uso pessoal (resources_fm_banks.cc) não integram esta distribuição. Scripts originais dos portes ficam em tools/port; a origem/patches estão documentados em seus cabeçalhos e o código resultante editável acompanha este pacote.

0.1.45-alpha.2: correção de áudio no iOS/iPadOS em web/app.js (Audio Session API em "playback", <audio> silencioso como alternativa, retomada do contexto interrompido). Sem mudança de DSP, WASM ou fontes nativas em relação à alpha.1; nome do cache do service worker atualizado.
