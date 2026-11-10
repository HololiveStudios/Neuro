# Pestinha C — Documentação e Relatório Técnico de Portabilidade

Port autocontido em C99 do **Pestinha**, reimplementando a arquitetura originalmente desenvolvida em Python (PyQt6/NumPy) diretamente sobre `libxcb` e renderização de software (canvas 2D próprio), sem qualquer dependência do runtime Python, Qt ou bibliotecas pesadas de interface.

---

## 1. Conflito `gfx` vs `fx` Resolvido

### O que causou o problema?
Durante a portabilidade inicial, os arquivos de efeitos visuais de tela cheia (`fx.h` e `fx.c`) foram renomeados inadvertidamente para `gfx.h` e `gfx.c`.
Isso causou colisão e ausência de componentes:
1. **Identidade trocada**: O código interno continuava definindo a estrutura `Fx`, as funções `fx_*` (cartazes, fogos, bolinhas de papel, notebook e bolinha de borracha) e fazendo `#include "fx.h"`.
2. **Módulo gráfico ausente**: Os módulos `fx.c`, `ui.c` e `xutil.h` incluíam `"gfx.h"` esperando uma biblioteca de desenho 2D em memória (o tipo `Canvas`, primitivas de preenchimento, círculos, bordas, medição de texto `gfx_text_width`, quebra de linha `gfx_wrap` e fonte bitmap embutida).

### Solução implementada
- **`fx.h` e `fx.c` restaurados**: Responsáveis pela camada de efeitos de desktop recortada via extensão SHAPE do X11 (cartazes de protesto, bolinhas de papel da revolução, notebook minúsculo, fogos de artifício e bolinha física interativa).
- **`gfx.h` e `gfx.c` criados do zero**: Motor gráfico 2D em software completo:
  - Estrutura `Canvas` (`w`, `h`, `uint32_t *px`) no padrão ARGB ZPixmap.
  - Funções de ciclo de vida: `canvas_init`, `canvas_free`.
  - Primitivas gráficas com recorte seguro (clipping): `canvas_fill`, `canvas_rect`, `canvas_disc`, `canvas_border`.
  - Utilitários de cor: `gfx_rgb(r, g, b)`.
  - Motor de texto: `gfx_text_width`, `gfx_wrap` (com suporte a quebra por palavras e limite de buffer de 64 bytes) e `canvas_text` com tabela de glifos bitmap embutida 8x12 e normalização de caracteres acentuados da língua portuguesa (UTF-8).

---

## 2. Subsistemas Implementados e Unificados

### 2.1 Display Desktop e Recorte XCB (`display_xcb.c/.h`)
- Suporte a modo janela clássica (100×100) e modo **Desktop Flutuante Recortado**:
  - `display_open_desktop(jorge, level)` nos 3 tamanhos clássicos: pequeno (90×68), médio (120×90) e grande (160×120).
  - Janela sem moldura (`override_redirect = 1`) com recorte vetorial arredondado via extensão `SHAPE` do X11 (`xu_shape_roundrect`).
  - Funções de geometria e plataforma: `display_size`, `display_screen`, `display_work_area`, `display_move` e `display_platforms`.
  - Renderizador dos 12 rostos clássicos de Bryan e Jorge, pupilas dinâmicas, gravata personalizada e piscadas naturais.
  - Validado pelo teste automatizado `test_display`.

### 2.2 Sensores Ambientais Reais (`sensors.c/.h`)
- Leitura não invasiva do ambiente do sistema operacional, preservando 100% a privacidade do usuário:
  - **Tempo ocioso (`idle_seconds`)**: Consulta nativa em milissegundos via protocolo `MIT-SCREEN-SAVER` do X11.
  - **Ritmo de digitação (`typing`)**: Detecta atividade de teclado sem registrar nenhuma tecla digitada.
  - **Movimento do cursor (`motion`)**: Monitoramento contínuo da velocidade e aceleração do mouse.
  - **Áudio ativo (`sound`)**: Inspeção recursiva de `/proc/asound` (e suporte a `$PESTINHA_ASOUND` em testes) para detecção de streams de áudio em reprodução (`RUNNING`).
  - **Classificação de aplicativo ativo**: Mapeamento de `_NET_ACTIVE_WINDOW` e `WM_CLASS` em canais de trabalho (`work`), navegação web (`web`) e novidade contextual (`novelty`).
  - Validado pelo teste automatizado `test_sensors`.

### 2.3 Modo Desktop Integrado no Loop Principal (`pestinha.c`)
- O executável principal integra todos os módulos em um único binário unificado:
  - Execução via `--desktop` (pet flutuando sobre a tela) ou modo janela clássica (100×100).
  - **Física inercial 2D**: Gravidade, quique elástico, atrito no solo e pouso no topo das janelas reais abertas pelo usuário (`display_platforms`).
  - **Interação por mouse**: O pet pode ser segurado e arrastado com o botão esquerdo (`body_grab`, `body_drag`, `body_release`) e arremessado com inércia balística.
  - **Menu de contexto completo**: Clique com o botão direito no pet abre o menu nativo da interface (`ui.c`) com opções de carinho, cutucar, trabalhar, dispensar, castigo, bolinha de brinquedo, permissões e sair.
  - **Efeitos de mundo automáticos**:
    - Notebook minúsculo desenhado na tela quando em trabalho/foco (`fx_notebook`).
    - Placa vermelha do cantinho do castigo quando em punição (`FX_STYLE_SIGN`).
    - Cartazes de protesto erguidos em greve (`FX_STYLE_PROTEST`).
    - Fogos de artifício em momentos de euforia e alta alegria (`fx_fireworks`).
    - Bolinha de borracha que o pet chuta ao se aproximar e que o usuário pode arremessar (`fx_ball_*`).

---

## 3. Mapeamento Exaustivo: Python Original ➔ Port em C

| Módulo Original Python (`pestinha/`) | Equivalente em C (`certinho/`) | Funções e Responsabilidades | Status |
|---|---|---|:---:|
| `core/nervous_system.py` | `neuro.c/.h`, `network_data.h` | Rede LIF de 470 neurônios, 30 canais sensoriais, 15 motores, plasticidade trifatorial STDP | **Concluído** |
| `core/homeostasis.py` | `homeostasis.c/.h` | Balanço de carência, cansaço, tédio e energia | **Concluído** |
| `core/conditioning.py` | `conditioning.c/.h` | Associação temporal de pistas e antecipação | **Concluído** |
| `core/episodic.py` | `memory.c/.h` | Memória contextual, bandit de ações e histórico com persistência atômica | **Concluído** |
| `core/emotion.py`, `core/motor.py` | `behavior.c/.h` | Decodificação motora, estados afetivos e seleção de rostos | **Concluído** |
| `core/physics.py` | `body.c/.h` | Física 2D, gravidade, quique, pouso em janelas e arrasto | **Concluído** |
| `core/protesto.py` | `behavior.c`, `fx.c` | Insatisfação, greve e exibição de cartazes | **Concluído** |
| `body/desenho_rosto.py` | `display_xcb.c/.h` | 12 expressões vetoriais rasterizadas, gravata de Jorge e olhos dinâmicos | **Concluído** |
| `body/behavior.py` | `body.c/.h` | Piloto autônomo (caminhada, fuga do mouse, cantinho, pulinhos) | **Concluído** |
| `body/bolinha.py`, `body/toy.py` | `fx.c/.h` | Bolinha de borracha arrastável e interação de chutes | **Concluído** |
| `world/cartazes.py`, `world/mischief.py` | `fx.c/.h` | Cartazes de protesto, bilhetes e post-its em camada recortada SHAPE | **Concluído** |
| `world/fireworks.py` | `fx.c/.h` | Sistema balístico de partículas coloridas de fogos de artifício | **Concluído** |
| `world/notebook.py` | `fx.c/.h` | Laptop desenhado com tela piscante e teclado | **Concluído** |
| `mind/castigo_ctl.py` | `body.c`, `behavior.c`, `fx.c` | Deslocamento para o canto inferior direito e placa de castigo | **Concluído** |
| `mind/revolucao.py` | `fx.c/.h` | Bolinhas de papel arremessadas na tela | **Concluído** |
| `mind/sleep.py`, `mind/mesa.py` | `behavior.c`, `fx.c` | Máquina de sono e postura de trabalho no notebook | **Concluído** |
| `sense/senses.py`, `plataforma/linux.py` | `sensors.c/.h`, `xutil.c/.h` | Sensores ambientais via XCB, MIT-SCREEN-SAVER e /proc/asound | **Concluído** |
| `ui/ui.py` | `ui.c/.h`, `gfx.c/.h` | Menu de clique direito, diálogo de privacidade e atalho de pânico Ctrl+Alt+P | **Concluído** |
| `app.py` | `pestinha.c` | Orquestrador principal com loop síncrono calibrado a ~30 fps | **Concluído** |

---

## 4. Compilação e Execução

### Dependências (Linux)
- Compilador C99 (`gcc` ou `clang`).
- Bibliotecas: `libm` e `libxcb` (`sudo apt install libxcb1-dev libxcb-shape0-dev`).

### Comandos de Compilação e Testes
```sh
# Compilação completa do binário integrado
make

# Execução da suíte de 4 testes automatizados
make test

# Limpeza de binários gerados
make clean
```

### Como Executar
```sh
# Iniciar no MODO DESKTOP FLUTUANTE (pet livre na tela inteira com física, sensores e menus)
./pestinha --desktop

# Iniciar no MODO DESKTOP com o pet Jorge
./pestinha --desktop --pet jorge

# Iniciar no MODO JANELA CLÁSSICA (caixa 100x100 para testes rápidos com teclado)
./pestinha

# Visualizar todas as 12 expressões faciais em sequência
./pestinha --faces

# Exibir telemetria e taxas de disparo da rede neural em tempo real
./pestinha --demo

# Executar avaliação determinística de aprendizado e CPU
./pestinha --evaluate

# Exibir métricas e memórias salvas no disco
./pestinha --metrics
```

### Controles no Modo Desktop
- **Botão Esquerdo do Mouse**: Segure sobre o pet para arrastá-lo pela tela; solte em movimento rápido para arremessá-lo com física balística.
- **Botão Direito do Mouse**: Clique sobre o pet para abrir o menu suspenso de ações (Carinho, Cutucar, Trabalhar, Dispensar, Castigo, Bolinha, Sensores, etc.).
- **Atalho Global de Pânico**: `Ctrl+Alt+P` esconde o pet e congela os sensores instantaneamente.

### Teclas no Modo Janela
| Tecla | Ação | Efeito no Pet |
|:---:|---|---|
| **C** | Carinho | Estimula canal de afeto (+ recompensa STDP na rede) |
| **P** | Cutucada | Interrupção/estímulo tátil |
| **W** | Trabalho | Coloca o pet em modo foco/notebook |
| **X** | Dispensar | Penalização leve por interrupção |
| **L** | Cantinho | Envia o pet ao cantinho do castigo |
| **V** | Visita | Visita ao pet no cantinho |
| **F** | Soltar | Libera o pet do castigo |
| **M** | Travessura | Provoca uma ação de travessura contextual |
| **A** | Arremessar | Impulso de lançamento físico |
| **T** | Avanço de Tempo | Avança 10 segundos no relógio de simulação |
| **Q** | Sair | Salva o estado dos arquivos `.brain`, `.memory` e finaliza |
