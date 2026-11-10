#ifndef PESTINHA_DISPLAY_XCB_H
#define PESTINHA_DISPLAY_XCB_H

#include <xcb/xcb.h>
#include "body.h"

typedef struct PestinhaDisplay PestinhaDisplay;

/* Modo janela fixa 100x100 para testes e janela padrão */
PestinhaDisplay *display_open(int jorge);

/* Modo desktop flutuante e recortado (SHAPE) nos tamanhos:
   level 0: pequeno (90x68)
   level 1: médio/padrão (120x90)
   level 2: grande (160x120) */
PestinhaDisplay *display_open_desktop(int jorge, int level);

void display_close(PestinhaDisplay *display);
int  display_poll(PestinhaDisplay *display);

void display_size(PestinhaDisplay *d, int *w, int *h);
void display_screen(PestinhaDisplay *d, int *sw, int *sh);
void display_work_area(PestinhaDisplay *d, Area *wa);
void display_move(PestinhaDisplay *d, int x, int y);
int  display_platforms(PestinhaDisplay *d, int min_top, Platform *out, int max);

xcb_connection_t *display_connection(PestinhaDisplay *d);
xcb_window_t      display_window(PestinhaDisplay *d);

void display_draw(PestinhaDisplay *display, int face, double energy,
                  double cheer, double anger, double sadness, int corner,
                  int sleeping, double elapsed);

#endif
