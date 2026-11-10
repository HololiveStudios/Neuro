# Pestinha C

**Um pet virtual com comportamento autônomo, memória e interação com o ambiente desktop.**

O Pestinha é um projeto de pet virtual originalmente desenvolvido em Python e posteriormente portado para **C99**, preservando as funcionalidades e os comportamentos da versão original.

A nova implementação foi desenvolvida para ser extremamente leve, reduzindo drasticamente o tamanho do executável e o consumo de memória, sem abandonar os recursos que fazem parte da experiência original.

## Características

- Rede neural com 470 neurônios.
- Memória contextual e aprendizado por associação.
- Comportamento autônomo e estados emocionais.
- Física 2D com gravidade, colisões e inércia.
- Movimentação e interação com o mouse.
- Reações ao ambiente e à atividade do usuário.
- Expressões faciais e animações.
- Efeitos visuais, partículas e interações.
- Persistência de memória entre execuções.

## Desempenho

O port em C foi desenvolvido com foco em eficiência e baixo consumo de recursos.

| Indicador | Resultado observado |
|---|---:|
| Tamanho do executável | **126 KB** |
| Consumo de RAM | **Aproximadamente 4 MB** |
| Linguagem | C99 |
| Renderização | Software, canvas 2D próprio |
| Interface gráfica | X11/XCB |

*Os valores de tamanho e memória correspondem à versão e ao ambiente em que foram observados. O consumo pode variar conforme o sistema e a execução.*

## Compatibilidade atual

Atualmente, o Pestinha está funcionando em:

- Linux com servidor gráfico X11.
- Integração gráfica por XCB.

Esta versão ainda não possui suporte nativo a Wayland ou Windows.

## Como executar

O programa é distribuído como executável. Não é necessário instalar Python, Qt ou outras dependências dessas tecnologias antigas.

No terminal, execute:

```bash
chmod +x ~/Documentos/pestinha && ~/Documentos/pestinha
```

## Um projeto em evolução

O Pestinha demonstra como uma aplicação originalmente desenvolvida em Python pode ser reimplementada em C, preservando suas funcionalidades e reduzindo significativamente sua necessidade de recursos.

**Menos de 130 KB de executável. Aproximadamente 4 MB de RAM. As funcionalidades do pet original, agora em C.**
