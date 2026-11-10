/*
 * display.h — Interface pública da janela da Pestinha.
 *
 * Sem dependência de XCB, X11 ou qualquer plataforma.
 * Implementado em display.c via SDL2.
 *
 * GPL-3.0-or-later
 */
#ifndef PESTINHA_DISPLAY_H
#define PESTINHA_DISPLAY_H

#include "body.h"   /* Area, Platform */

typedef struct PestinhaDisplay PestinhaDisplay;

/* Modo janela normal 100×100 */
PestinhaDisplay *display_open(int jorge);

/* Modo desktop flutuante sem borda
   level 0: 90×68   level 1: 120×90   level 2: 160×120 */
PestinhaDisplay *display_open_desktop(int jorge, int level);

void display_close(PestinhaDisplay *d);

/*
 * Coleta eventos pendentes.
 * Retorna -1 se a janela foi fechada,
 *          0 se não há evento relevante,
 *          char ASCII da tecla se uma tecla mapeada foi pressionada.
 */
/*
 * Eventos de mouse coletados por display_poll.
 * display_poll preenche este struct quando um evento de mouse ocorre
 * e retorna 1. Quando retorna 0, não houve evento relevante.
 */
typedef enum {
    DISPLAY_EV_NONE = 0,
    DISPLAY_EV_BUTTON_PRESS,
    DISPLAY_EV_MOTION,
    DISPLAY_EV_BUTTON_RELEASE
} DisplayMouseEvent;

typedef struct {
    int type;          /* DisplayMouseEvent */
    int button;        /* 1 = esquerdo, 3 = direito */
    int root_x, root_y; /* coordenadas globais de tela */
    int win_x, win_y;   /* coordenadas relativas à janela principal */
} DisplayMouse;

int display_poll(PestinhaDisplay *d);
/* Se houver evento de mouse pendente, preenche *m e retorna 1. */
int display_poll_mouse(PestinhaDisplay *d, DisplayMouse *m);

void display_size(PestinhaDisplay *d, int *w, int *h);
void display_screen(PestinhaDisplay *d, int *sw, int *sh);
void display_work_area(PestinhaDisplay *d, Area *wa);
void display_move(PestinhaDisplay *d, int x, int y);

/*
 * Lista janelas visíveis que podem servir de plataforma.
 * SDL2 não tem API para enumerar janelas de outros processos:
 * retorna sempre 0 (sem plataformas detectadas).
 * Manter a assinatura para não quebrar callers existentes.
 */
int display_platforms(PestinhaDisplay *d, int min_top, Platform *out, int max);

/* ---- Janelas secundárias (menu, dialogo, fx) ---- */

typedef struct DispWindow DispWindow;

/* Cria janela flutuante sem borda, sempre no topo, fora da barra de tarefas.
 * sx, sy: posição global; w, h: tamanho.Usado por ui.c e fx.c. */
DispWindow *disp_window_open(int sx, int sy, int w, int h);
void disp_window_close(DispWindow *w);
void disp_window_move(DispWindow *w, int x, int y);
void disp_window_raise(DispWindow *w);
void disp_window_set_visible(DispWindow *w, int visible);
int  disp_window_is_visible(DispWindow *w);
void disp_window_get_size(DispWindow *w, int *w, int *h);

/*
 * Blit um Canvas (gfx.h) na janela secundária.
 * Equivalente a xu_put_canvas.
 */
void disp_window_put_canvas(DispWindow *w, const Canvas *cv,
                            int sx, int sy, int sw, int sh, int dx, int dy);

/* Processa eventos de uma janela secundária. Retorna 1 se houve ação. */
int disp_window_poll(DispWindow *w, DisplayMouse *m);

/* Marca a janela para ser redesenhada no próximo disp_window_present. */
void disp_window_invalidate(DispWindow *w);

void display_draw(PestinhaDisplay *d, int face, double energy,
                  double cheer, double anger, double sadness, int corner,
                  int sleeping, double elapsed);

#endif /* PESTINHA_DISPLAY_H */
