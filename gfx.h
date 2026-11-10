/*
 * gfx.h — Software canvas com rastreamento de região suja.
 *
 * Toda primitiva de desenho expande automaticamente o dirty rect.
 * Antes de chamar render_backend.present(), consulte canvas_get_dirty()
 * para enviar apenas a região que mudou — evita copiar o framebuffer
 * inteiro quando só a expressão facial se altera.
 *
 * Formato de pixel: 0x00RRGGBB (32 bpp, byte mais alto ignorado).
 *
 * GPL-3.0-or-later
 */
#ifndef PESTINHA_GFX_H
#define PESTINHA_GFX_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Canvas                                                              */
/* ------------------------------------------------------------------ */

typedef struct Canvas {
    int       w, h;
    uint32_t *px;

    /*
     * Menor retângulo que contém todos os pixels modificados desde a
     * última chamada a canvas_clear_dirty().
     * dirty == 0 significa "nada mudou"; as coordenadas são indefinidas.
     */
    int dirty;
    int dirty_x0, dirty_y0;   /* canto superior-esquerdo (inclusive) */
    int dirty_x1, dirty_y1;   /* canto inferior-direito  (exclusive) */
} Canvas;

/* ------------------------------------------------------------------ */
/* Cor                                                                 */
/* ------------------------------------------------------------------ */

uint32_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b);

/* ------------------------------------------------------------------ */
/* Ciclo de vida                                                       */
/* ------------------------------------------------------------------ */

void canvas_init(Canvas *c, int w, int h);
void canvas_free(Canvas *c);

/* ------------------------------------------------------------------ */
/* Dirty rect                                                          */
/* ------------------------------------------------------------------ */

/* Zera o dirty rect (chamar após present). */
void canvas_clear_dirty(Canvas *c);

/*
 * Preenche *x, *y, *w, *h com o dirty rect e retorna 1.
 * Retorna 0 se canvas_clear_dirty() foi chamado sem nenhum desenho
 * posterior (nada a enviar).
 */
int canvas_get_dirty(const Canvas *c, int *x, int *y, int *w, int *h);

/* ------------------------------------------------------------------ */
/* Primitivas (atualizam o dirty rect automaticamente)                */
/* ------------------------------------------------------------------ */

void canvas_fill  (Canvas *c, uint32_t color);
void canvas_rect  (Canvas *c, int x, int y, int w, int h, uint32_t color);
void canvas_disc  (Canvas *c, int cx, int cy, int r, uint32_t color);
void canvas_border(Canvas *c, int x, int y, int w, int h,
                   int thickness, uint32_t color);

/* ------------------------------------------------------------------ */
/* Texto com fonte bitmap embutida                                     */
/*                                                                     */
/* scale = 0 : tamanho normal  (~8×12 px por glifo, avanço  8 px)    */
/* scale = 1 : tamanho grande  (~16×24 px por glifo, avanço 16 px)   */
/* ------------------------------------------------------------------ */

int  gfx_text_width(const char *text, int scale);
int  gfx_wrap(const char *text, int maxw, int scale,
              char out[][64], int max_lines);
void canvas_text(Canvas *c, int x, int y, const char *text,
                 uint32_t color, int scale);

#endif /* PESTINHA_GFX_H */
