/*
 * render.h — Camada de apresentação independente de plataforma.
 *
 * Arquitetura inspirada no I_Video do Doom:
 *   A simulação e o desenho só enxergam um framebuffer uint32_t*.
 *   O backend cuida de janela, eventos e blit para a tela.
 *   Trocar XCB por DRM/KMS ou qualquer outra saída exige apenas
 *   uma nova implementação desta interface — zero alteração no resto.
 *
 * GPL-3.0-or-later
 */
#ifndef PESTINHA_RENDER_H
#define PESTINHA_RENDER_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Eventos                                                             */
/* ------------------------------------------------------------------ */

typedef enum {
    RENDER_EV_NONE = 0,
    RENDER_EV_CLOSE,         /* pedido de fechar a janela              */
    RENDER_EV_EXPOSE,        /* janela exposta — redesenhar tudo       */
    RENDER_EV_MOUSE_MOVE,    /* movimento do ponteiro                  */
    RENDER_EV_MOUSE_DOWN,    /* botão pressionado                      */
    RENDER_EV_MOUSE_UP,      /* botão liberado                         */
    RENDER_EV_KEY_DOWN,      /* tecla pressionada (keycode nativo)     */
    RENDER_EV_KEY_UP,        /* tecla liberada                         */
} RenderEventType;

typedef struct {
    RenderEventType type;
    int x, y;      /* posição do cursor (eventos de mouse)   */
    int button;    /* 1=esq  2=meio  3=dir  (mouse)          */
    int key;       /* keycode nativo do backend (teclado)    */
} RenderEvent;

/* ------------------------------------------------------------------ */
/* Configuração de abertura                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    int         w, h;       /* dimensões do framebuffer lógico        */
    int         desktop;    /* 1 = modo flutuante sem decoração       */
    int         level;      /* tamanho (0=pequeno 1=médio 2=grande)   */
    int         jorge;      /* flag de temperamento (aparência)        */
    const char *title;      /* título da janela (pode ser NULL)        */
} RenderConfig;

/* ------------------------------------------------------------------ */
/* Interface do backend                                                */
/* ------------------------------------------------------------------ */

typedef struct RenderBackend {
    const char *name;

    /*
     * Abre a janela/saída.
     * Retorna 1 em sucesso, 0 em falha.
     */
    int  (*init)(const RenderConfig *cfg);

    /* Fecha a janela e libera todos os recursos do backend. */
    void (*shutdown)(void);

    /*
     * Coleta eventos pendentes sem bloquear.
     * Retorna o número de eventos escritos em out[] (máx max_ev).
     */
    int  (*poll)(RenderEvent *out, int max_ev);

    /*
     * Envia o framebuffer para a tela.
     *
     *   pixels — array de w×h pixels em formato 0x00RRGGBB, row-major.
     *   w, h   — dimensões do buffer (devem corresponder ao cfg).
     *   dirty  — retângulo sujo {x, y, largura, altura} ou NULL para
     *            reenviar a janela inteira.  Backends que não suportam
     *            atualização parcial devem ignorar dirty e redesenhar
     *            tudo, sem travar.
     */
    void (*present)(const uint32_t *pixels, int w, int h,
                    const int dirty[4]);

    /* Move a janela (modo desktop). Ignorado em modo janela normal. */
    void (*move)(int x, int y);

    /* Preenche *w e *h com as dimensões atuais da janela/saída. */
    void (*window_size)(int *w, int *h);

    /* Preenche *w e *h com as dimensões da tela/monitor principal. */
    void (*screen_size)(int *w, int *h);

} RenderBackend;

/* ------------------------------------------------------------------ */
/* Backends disponíveis                                                */
/* ------------------------------------------------------------------ */

/*
 * render_backend_sdl2  — Linux X11/Wayland + Windows + macOS (render_sdl2.c)
 * render_backend_xcb   — Linux/X11 via XCB + MIT-SHM (render_xcb.c)
 * render_backend_null  — headless, descarta tudo  (render_null.c)
 *
 * Para compilação cross-platform use render_backend_sdl2.
 * render_backend_xcb é mantido apenas para compatibilidade com o
 * display_xcb.c existente enquanto a migração não estiver completa.
 *
 * Adicionar um novo backend: implementar as funções acima e preencher
 * uma RenderBackend estática; não modificar nenhum outro arquivo.
 */
extern RenderBackend render_backend_sdl2;
extern RenderBackend render_backend_xcb;
extern RenderBackend render_backend_null;

#endif /* PESTINHA_RENDER_H */
