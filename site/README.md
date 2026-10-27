# Usina Web / Android Lab 0.1.46

O APK utiliza o motor C++ do Usina atual, compilado para WebAssembly, e funciona offline.
A navegação principal segue SYNTH / FX / SEQ / CABINE. Licenças e créditos ficam disponíveis no MENU.

## Nesta versão

- SEQ MONO/POLY: MONO filtra notas extras na entrada armada e toca a nota base dos acordes, sem apagá-los.
- 32 escalas e tônica do Ruído Core para entrada, edição e geração. Escolher escala preserva o padrão;
  APLICAR ESCALA quantiza explicitamente as notas já gravadas.
- ACCENT, LATE, CONDITION, BASE e GATE até 100%, conforme a página SEQ nativa.
- EUCLID, RANDOM, MUTATE, CLEAR e rotação com os comandos C++ compartilhados com o plugin, em worker separado
  da reprodução. RANDOM/MUTATE preservam intervalos dos acordes; a escala orienta sua nota base.
- Sessão JSON v5 com leitura de v1/v2/v3/v4, incluindo expressão por passo, Mono/Poly e escala por pista.

- Oitavas por OCT −/+ e indicação musical; seletores RATIO/FIXED e botão do topo sem texto longo.
- Scopes ao vivo de ENV, LFO, RAND e COMB, com valores do motor; indicador de FUNC sobre a curva.
- SEQ com PASSOS / TECLADO / PIANO ROLL sobre o mesmo padrão, notas nomeadas e controles por arraste.
- Entrada por passos polifônica: arme ENTRAR, toque o acorde, solte todas as teclas para avançar.
- Até 16 notas por passo; VEL, probabilidade e ratchet pertencem ao passo; cada nota pode ter duração própria, ou herdar GATE. No piano roll, arrastar
  combina notas no destino; APAGAR remove a nota selecionada. Sessão JSON v5, com leitura de v1–v4.
- CABINE MELODY mantém seu gesto original: atribuir uma tecla substitui o passo por essa nota.

- Seis operadores, algoritmos desenhados a partir do motor nativo, matriz USER e envelopes editáveis.
- Famílias MOD e editor gráfico de funções; Learn/Forget e Pickup; mapas XML .usinamap.
- Teclado local com modos GLISS e MPE. Em MPE: X = bend por dedo, Y = pressão e CC74 em strip separada.
- Canais 2–16 independentes, inclusive duas notas iguais; RPN, sustain e cancelamento/pausa.
- Barras coloridas indicam direção e deslocamento do bend. Pressão e TIMBRE exigem rotas, como no plugin.
- Controlador MPE externo separado no MENU; no Android, saídas MIDI USB/OTG e instrumentos BLE conectados.
- CABINE SESSION, DRUMS, MELODY, KEYS e USER; sequenciador de nove pistas.
- Biblioteca categorizada e importação de sons .usina. Sessões JSON e presets do plugin são formatos distintos.

MPE inicia desligado. Os alcances master e por nota são configuráveis em Configurações ou MOD → CONTROLLERS.
O controle de CC74 do teclado aplica seu valor às notas mantidas e às novas notas.
Ocultar/redimensionar o teclado libera suas notas locais sem um panic global.

O iPad usa a versão web por HTTPS, com áudio e toque; MIDI USB/BLE do Safari não faz parte desta entrega.
Android é o alvo principal. Ainda são necessárias validações físicas no A34/Poco X7 e no iPad de 10ª geração.

Ainda pendentes: modulação dos macros/ROOM/master, sync MIDI, transferência direta de presets, sensores,
câmera/morph/bancos e convolução IR. O Android não anuncia um periférico BLE MIDI.

## Desenvolvimento e validação

- `mobile/build-android.ps1`: compila WASM, prepara assets, sincroniza Capacitor e gera APK de teste.
- `mobile/test-android.ps1`: executa regressões offline num emulador dedicado e vincula relatório ao hash do APK.
- `node web/tests/keybed-state-test.mjs`: propriedade de dedos/notas, GLISS, MPE, RPN e cancelamento.
- `node web/tests/mpe-audio-test.mjs`: áudio WASM, independência de pitch/pressão/CC74, sustain, RPN e caudas.
- `web/test.mjs --all`, em WSL após compilar a referência nativa: comparação dos 624 renders.
- `web/tests/preset-import-test.mjs`: preset/áudio exportados pelo AudioProcessor JUCE atual.
- `web/package.py`: rejeita DSP/assets/relatórios incompatíveis antes de empacotar.

A comparação nativo/WASM usa o mesmo adaptador nos dois alvos. O cenário de importação JUCE é uma verificação
adicional específica, não uma demonstração de paridade integral de todo o AudioProcessor.

CABINE DRUM: toque liga/desliga; arraste vertical ajusta ACCENT continuamente. Trocar a oitava do teclado local preserva as notas sustentadas. O acelerômetro continua planejado.

Piano roll: dois dedos navegam e ampliam os eixos de tempo/altura; um dedo move notas; borda direita ajusta a duração própria da nota. Edição é confirmada ao soltar; cancelar ou iniciar navegação preserva o padrão. Cada nota pode durar de 0,01 a 64 passos da pista. Notas sem duração própria continuam herdando o GATE legado; HERDAR GATE restaura essa opção. Sessões JSON v5 abrem v1–v4. Pinch também está disponível na matriz ROTAS e nos scopes; envelopes editáveis também têm pinch; demais superfícies continuam pendentes.

CABINE ROTAS: dois dedos ampliam e navegam na matriz, sem alterar as rotas. O botão de porcentagem restaura 100%. A faixa de macros preserva o toque imediato e bloqueia o início da navegação enquanto estiver sendo tocada. As demais camadas mantêm seus gestos de performance.

Scopes ENV/LFO/RAND/COMB: pinch horizontal amplia o tempo; vertical amplia a escala visual; deslocar os dois dedos navega no histórico disponível. O gráfico mostra tempo, ampliação e deslocamento. 1:1 restaura 5 s/×1. Isso não altera parâmetros nem a saída de modulação.

FX: um editor do slot selecionado, com três seletores e botões de ligar/desligar por barramento. ROUTING permanece acessível; FEEDBACK/TIME pertencem ao LOOP INSERT como no plugin. I/O global continua no botão próprio. Arrastar o número troca slots, inclusive entre barramentos: durante o arraste surgem os destinos FX A/B/AUX/LOOP. O editor acompanha o efeito movido. O port IR permanece pendente.

DISTORT: o nome e a unidade de CHARACTER seguem cada modo nativo (EVEN/ASYM/STARVE/EDGE/FULL/CRUSH/MASK/BITS). DELAY: TIME é atenuado visualmente quando sincronizado; WOW/FLUTTER são atenuados no modo Digital. Os valores são preservados.

Envelopes editáveis: dois dedos ampliam/navegam a visualização; um dedo ajusta tempos/sustain como antes. Após iniciar a edição, dedos extras não assumem a navegação. 1:1 restaura a vista inteira. Zoom é visual e não modifica os parâmetros.

Editor FUNC: dois dedos ampliam/navegam sem alterar pontos/curvas. Um dedo preserva arraste de pontos e alças; duplo toque mantém adicionar/remover; SNAP/SHIFT/REPEAT permanecem. 1:1 restaura a vista.

DELAY: TAP no editor do slot reutiliza o histórico da CABINE. Livre ajusta milissegundos; SYNC escolhe a divisão musical mais próxima. O botão mostra o resultado; ações MIDI atualizam o mesmo estado.

CABINE DRUMS: barra de accent por passo; atualizações do passo isolado durante o arraste. Vibração opcional limitada por deslocamento e extremos.

TEXTURE mantém grupos TEXTURE/GRAINS/SEED/MIX do plugin. Leituras especiais de TEXTURE e pitch VAPOR são geradas do processor JUCE, por posição legal do knob. DELAY e DISTORT exibem unidades nativas. TAP usa o instante de pressão, sem contar novamente a soltura.

MOD / LFO: atalhos nativos SINE/TRI/SQR/SAW DN/SAW UP/S&H e TAP compartilhado com CABINE/MIDI. Livre ajusta SPEED; SYNC seleciona divisão. FADE, SPEED e WAVE B indicam os estados nativos de KEY SYNC/SYNC/MORPH sem reconstruir o controle durante o gesto. LINK DEC/REL disponível nos envelopes da página MOD, conforme o plugin.

LINK DEC / REL: vínculo de interface entre DECAY e RELEASE do envelope selecionado em MOD. Ativar não iguala valores; editar um conduz o outro, com prioridade de DECAY se ambos mudam juntos. Selecionar outro envelope preserva seus valores. O vínculo continua enquanto outra página está aberta, sem acrescentar campo ao preset ou aos envelopes de operador.

FX: VAPOR, DELAY, DISTORT e ROOM seguem as fileiras do painel nativo, com seletores/interruptores separados dos knobs e TAP junto de SYNC/FREEZE no DELAY. VAPOR mantém as duas fileiras de cinco e quatro parâmetros e captions por modo; DISTORT mantém seus seis controles; ROOM mantém AMOUNT e COLOR lado a lado. Em pouca largura, os knobs redistribuem-se dentro da mesma fileira lógica.

Nos painéis FX, switches mantêm o nome da função no botão e seu estado por destaque/aria-checked. Seletores MODE/QUALITY e CHARACTER/ROUTING mantêm nome acessível sem repetir título visual, conforme o plugin. Power dos slots continua ON/OFF.

Operadores: nomes/unidades de A/B/C/D seguem modeValue do plugin, com sinais e arredondamento nativos; resumo de Ratio/Fixed conserva as leituras de ratioText. MODAL mostra densidade em modes e BRIGHT/STIFF em porcentagem bipolar. Filtro: SK CASC/CMOS têm grupo próprio, DRIVE segue a topologia nativa; controles principais e gráfico de EG2 atenuam com FILTER desligado, mantendo edição.


Piano roll 0.1.35: arrastar a borda direita de uma nota selecionada ajusta as durações de todo o grupo com o mesmo deslocamento, limitado pelo membro mais curto/longo. Dois dedos ou cancelamento descartam a prévia. COPIAR/COLAR/DUPLICAR preservam a expressão; conflitos por passo são bloqueados também no movimento de uma única nota.

DESFAZER/REFAZER inclui EUCLID/RANDOM/MUTATE/CLEAR/rotação/APLICAR ESCALA. Refazer restaura o resultado já calculado. Uma resposta pendente só se aplica à sessão/pista de origem se elas não mudaram. Ctrl/Cmd+A seleciona a pista, C copia, V cola na régua, D duplica, Z desfaz, Shift+Z/Y refaz; setas movem, Delete/Backspace apagam e Esc limpa seleção. Atalhos limitados à superfície e ferramentas do roll; campos de texto mantêm seus comandos.


0.1.35: teclado físico A/W/S/E/D/F/T/G/Y/H/U/J/K funciona após foco em botões e sliders, preservando campos de texto, edição de seletores e atalhos. Escolha feita por ponteiro libera o foco após mudança confirmada. BEND/MOD confirmam a posição final e liberam a captura no reset; eventos atrasados não sobrescrevem o readback MIDI.

SYNTH expandido: MODE/WAVE nos seis operadores, conforme o plugin; modo compacto mantém WAVE no editor detalhado. Estados sem uso de WAVE permanecem atenuados e editáveis. Controles de ponteiro preciso usam o diâmetro nativo de 46 px; painéis preservam as fileiras com redistribuição conforme espaço/tipo de entrada.


Gráfico do filtro: a última curva medida permanece visível durante ajustes de cutoff/ressonância e controles CASCADE. Medições no worker continuam durante o gesto, com um cálculo ativo e somente o pedido pendente mais recente, no máximo oito submissões por segundo. Interpolação visual de 100 ms entre medições; endpoint é a resposta medida. Topologia/taxa/BYPASS invalidam a família anterior; respostas não estabilizadas continuam sinalizadas. O gráfico recebe foco de teclado ao tocar, mantendo ASDF disponível. Nada disso altera processamento ou suavização do áudio.


0.1.35: MOD preserva fileiras e a relação entre editor/scope e controles das famílias nativas. FUNC SPEED/SUSTAIN, RAND SPEED e COMB SOURCE/MOD indicam seus estados dependentes em tempo real, sem recriar gráficos. O marcador SUSTAIN de FUNC só aparece nos modos de envelope e respeita a quantidade de pontos. Fórmulas COMB atualizam no lugar. RAND conserva RISE/FALL em todos os tipos; são as suavizações já existentes, sem mudar DSP. MACRO 1–4 e A1–A3 mantêm profundidade acima do alvo nas três primeiras colunas.

## Revisão 0.1.44

TIMBRES → EXPORTAR salva o timbre em .usina para o plugin, incluindo parâmetros, macros, rotas e curvas FUNC. A sequência não é exportada: ao carregar, o plugin esvazia e desativa sua sequência. Para guardar os padrões do APK/web, salvar sessão JSON. Entrada/saída, qualidade e mapas MIDI do receptor são preservados; realimentação retorna a zero conforme o loader nativo.

Botões IMPORTAR MAPA / EXPORTAR MAPA alinhados; ajuda dos gestos A / D-S / R nos envelopes. Plano de CABINE e sketches em docs/CABINE_SKETCHES.md; direção de produto em docs/VISAO_PRODUTO.md.

Revisão 0.1.45: sete acessos explícitos no CABINE; MELODY separado de DRUMS. Selecionar a superfície já ativa mantém os gestos em curso; trocar de superfície encerra os gestos da anterior. Estado musical e mapas MIDI preservados.
