#ifndef PESTINHA_XUTIL_H
#define PESTINHA_XUTIL_H

#include <xcb/xcb.h>

#include "body.h"
#include "gfx.h"

/* Ajudantes de XCB usados pelos modulos do modo desktop. So dependem de libxcb:
   a extensao SHAPE e chamada direto pelo protocolo (sem libxcb-shape). */
#define XU_SHAPE_BOUNDING 0
#define XU_SHAPE_INPUT 2

int  xu_has_shape(xcb_connection_t *c);
/* Define a forma da janela como a uniao dos retangulos (n == 0: vazia). */
void xu_shape_set(xcb_connection_t *c, xcb_window_t win, int kind,
                  const xcb_rectangle_t *rects, int n);
void xu_shape_roundrect(xcb_connection_t *c, xcb_window_t win, int kind, int w, int h, int r);
void xu_shape_ellipse(xcb_connection_t *c, xcb_window_t win, int kind, int w, int h);
xcb_atom_t xu_atom(xcb_connection_t *c, const char *name);
void xu_put_canvas(xcb_connection_t *c, xcb_drawable_t win, xcb_gcontext_t gc, uint8_t depth,
                   const Canvas *canvas, int sx, int sy, int w, int h, int dx, int dy);
void xu_raise(xcb_connection_t *c, xcb_window_t win);
/* Area util da tela (_NET_WORKAREA, ou a raiz inteira). */
void xu_work_area(xcb_connection_t *c, xcb_window_t root, int sw, int sh, Area *out);
/* Janelas visiveis onde o pet pode pousar (topo da janela), de baixo para cima. */
int xu_list_platforms(xcb_connection_t *c, xcb_window_t root, int sw, int sh,
                      int min_top, Platform *out, int max);

#endif
