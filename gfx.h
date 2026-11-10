#ifndef PESTINHA_GFX_H
#define PESTINHA_GFX_H

#include <stdint.h>

/* Superfície de desenho em memória (software canvas) */
typedef struct Canvas {
    int w;
    int h;
    uint32_t *px;
} Canvas;

/* Cria cor empacotada ARGB (0x00RRGGBB) */
uint32_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b);

/* Ciclo de vida do canvas */
void canvas_init(Canvas *c, int w, int h);
void canvas_free(Canvas *c);

/* Primitivas de renderização 2D com recorte (clipping) seguro */
void canvas_fill(Canvas *c, uint32_t color);
void canvas_rect(Canvas *c, int x, int y, int w, int h, uint32_t color);
void canvas_disc(Canvas *c, int cx, int cy, int r, uint32_t color);
void canvas_border(Canvas *c, int x, int y, int w, int h, int thickness, uint32_t color);

/* Renderização e medição de texto com fonte bitmap embutida
   scale = 0: tamanho de texto padrão (~7x12px por glifo, espaçamento ~8px)
   scale = 1: título destacado (~14x24px por glifo, espaçamento ~16px) */
int  gfx_text_width(const char *text, int scale);
int  gfx_wrap(const char *text, int maxw, int scale, char out[][64], int max_lines);
void canvas_text(Canvas *c, int x, int y, const char *text, uint32_t color, int scale);

#endif
