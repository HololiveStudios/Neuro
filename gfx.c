/*
 * gfx.c — Rasterizador software com rastreamento de região suja.
 *
 * Mudanças em relação à versão anterior:
 *   • mark_dirty() inline integrado em todas as primitivas.
 *   • canvas_clear_dirty() / canvas_get_dirty() expostos.
 *   • canvas_init() inicializa o dirty rect como limpo.
 *
 * GPL-3.0-or-later
 */
#include "gfx.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Cor                                                                 */
/* ------------------------------------------------------------------ */

uint32_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* ------------------------------------------------------------------ */
/* Ciclo de vida                                                       */
/* ------------------------------------------------------------------ */

void canvas_init(Canvas *c, int w, int h)
{
    if (!c) return;
    c->w = w;
    c->h = h;
    c->px = (w > 0 && h > 0)
            ? (uint32_t *)calloc((size_t)w * (size_t)h, sizeof(uint32_t))
            : NULL;
    c->dirty    = 0;
    c->dirty_x0 = 0;
    c->dirty_y0 = 0;
    c->dirty_x1 = 0;
    c->dirty_y1 = 0;
}

void canvas_free(Canvas *c)
{
    if (!c) return;
    free(c->px);
    c->px = NULL;
    c->w  = c->h = 0;
    c->dirty = 0;
}

/* ------------------------------------------------------------------ */
/* Dirty rect                                                          */
/* ------------------------------------------------------------------ */

void canvas_clear_dirty(Canvas *c)
{
    if (c) c->dirty = 0;
}

int canvas_get_dirty(const Canvas *c, int *x, int *y, int *w, int *h)
{
    if (!c || !c->dirty) return 0;
    if (x) *x = c->dirty_x0;
    if (y) *y = c->dirty_y0;
    if (w) *w = c->dirty_x1 - c->dirty_x0;
    if (h) *h = c->dirty_y1 - c->dirty_y0;
    return 1;
}

/*
 * Expande o dirty rect para incluir o retângulo [x0,x1) × [y0,y1).
 * Coordenadas já devem estar recortadas pelo canvas (feito nas primitivas).
 */
static inline void mark_dirty(Canvas *c, int x0, int y0, int x1, int y1)
{
    if (x0 >= x1 || y0 >= y1) return;
    if (!c->dirty) {
        c->dirty_x0 = x0; c->dirty_y0 = y0;
        c->dirty_x1 = x1; c->dirty_y1 = y1;
        c->dirty = 1;
    } else {
        if (x0 < c->dirty_x0) c->dirty_x0 = x0;
        if (y0 < c->dirty_y0) c->dirty_y0 = y0;
        if (x1 > c->dirty_x1) c->dirty_x1 = x1;
        if (y1 > c->dirty_y1) c->dirty_y1 = y1;
    }
}

/* ------------------------------------------------------------------ */
/* Primitivas                                                          */
/* ------------------------------------------------------------------ */

void canvas_fill(Canvas *c, uint32_t color)
{
    int i, total;
    if (!c || !c->px) return;
    total = c->w * c->h;
    for (i = 0; i < total; ++i)
        c->px[i] = color;
    mark_dirty(c, 0, 0, c->w, c->h);
}

void canvas_rect(Canvas *c, int x, int y, int w, int h, uint32_t color)
{
    int x0, y0, x1, y1, row, col;
    if (!c || !c->px || w <= 0 || h <= 0) return;
    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x + w > c->w ? c->w : x + w;
    y1 = y + h > c->h ? c->h : y + h;
    if (x0 >= x1 || y0 >= y1) return;

    for (row = y0; row < y1; ++row) {
        uint32_t *line = c->px + row * c->w;
        for (col = x0; col < x1; ++col)
            line[col] = color;
    }
    mark_dirty(c, x0, y0, x1, y1);
}

void canvas_disc(Canvas *c, int cx, int cy, int r, uint32_t color)
{
    int y0, y1, dy, r2;
    int bx0 = cx - r, by0 = cy - r;
    int bx1 = cx + r + 1, by1 = cy + r + 1;

    if (!c || !c->px || r <= 0) return;
    r2 = r * r;
    y0 = cy - r < 0 ? 0 : cy - r;
    y1 = cy + r >= c->h ? c->h - 1 : cy + r;

    for (dy = y0 - cy; dy <= y1 - cy; ++dy) {
        int dyy     = dy * dy;
        int dx_max  = 0;
        int lx0, lx1, col;
        uint32_t *line;
        if (dyy > r2) continue;
        while ((dx_max + 1) * (dx_max + 1) + dyy <= r2)
            ++dx_max;
        lx0  = cx - dx_max < 0      ? 0      : cx - dx_max;
        lx1  = cx + dx_max >= c->w  ? c->w-1 : cx + dx_max;
        line = c->px + (cy + dy) * c->w;
        for (col = lx0; col <= lx1; ++col)
            line[col] = color;
    }

    /* dirty rect da bounding box clampada */
    mark_dirty(c,
        bx0 < 0      ? 0      : bx0,
        by0 < 0      ? 0      : by0,
        bx1 > c->w   ? c->w   : bx1,
        by1 > c->h   ? c->h   : by1);
}

void canvas_border(Canvas *c, int x, int y, int w, int h,
                   int thickness, uint32_t color)
{
    int t = thickness <= 0 ? 1 : thickness;
    if (!c || !c->px || w <= 0 || h <= 0) return;
    if (t * 2 >= w || t * 2 >= h) {
        canvas_rect(c, x, y, w, h, color);
        return;
    }
    canvas_rect(c, x,         y,         w,         t,         color);
    canvas_rect(c, x,         y + h - t, w,         t,         color);
    canvas_rect(c, x,         y + t,     t,         h - 2 * t, color);
    canvas_rect(c, x + w - t, y + t,     t,         h - 2 * t, color);
    /* dirty rect já foi expandido pelas canvas_rect acima */
}

/* ------------------------------------------------------------------ */
/* Fonte bitmap 8×12 para ASCII 32–126                                */
/* ------------------------------------------------------------------ */

static const uint8_t FONT_8X12[95][12] = {
    {0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0x18,0x18,0x18,0x18,0x18,0,0x18,0x18,0,0},
    {0,0x66,0x66,0x66,0,0,0,0,0,0,0,0},
    {0,0x66,0x66,0xff,0x66,0x66,0xff,0x66,0x66,0,0,0},
    {0,0x18,0x3e,0x60,0x3c,0x06,0x7c,0x18,0,0,0,0},
    {0,0x62,0x66,0x0c,0x18,0x30,0x66,0x46,0,0,0,0},
    {0,0x38,0x6c,0x38,0x76,0xdc,0xcc,0x76,0,0,0,0},
    {0,0x18,0x18,0x30,0,0,0,0,0,0,0,0},
    {0,0x0c,0x18,0x30,0x30,0x30,0x18,0x0c,0,0,0,0},
    {0,0x30,0x18,0x0c,0x0c,0x0c,0x18,0x30,0,0,0,0},
    {0,0,0x66,0x3c,0xff,0x3c,0x66,0,0,0,0,0},
    {0,0,0x18,0x18,0x7e,0x18,0x18,0,0,0,0,0},
    {0,0,0,0,0,0,0,0x18,0x18,0x30,0,0},
    {0,0,0,0,0x7e,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0x18,0x18,0,0,0},
    {0,0x02,0x06,0x0c,0x18,0x30,0x60,0x40,0,0,0,0},
    {0,0x3c,0x66,0x6e,0x76,0x66,0x66,0x3c,0,0,0,0},
    {0,0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0,0,0,0},
    {0,0x3c,0x66,0x06,0x1c,0x30,0x60,0x7e,0,0,0,0},
    {0,0x3c,0x66,0x06,0x1c,0x06,0x66,0x3c,0,0,0,0},
    {0,0x0c,0x1c,0x3c,0x6c,0x7e,0x0c,0x0c,0,0,0,0},
    {0,0x7e,0x60,0x7c,0x06,0x06,0x66,0x3c,0,0,0,0},
    {0,0x1c,0x30,0x60,0x7c,0x66,0x66,0x3c,0,0,0,0},
    {0,0x7e,0x06,0x0c,0x18,0x30,0x30,0x30,0,0,0,0},
    {0,0x3c,0x66,0x66,0x3c,0x66,0x66,0x3c,0,0,0,0},
    {0,0x3c,0x66,0x66,0x3e,0x06,0x0c,0x38,0,0,0,0},
    {0,0,0x18,0x18,0,0,0x18,0x18,0,0,0,0},
    {0,0,0x18,0x18,0,0,0x18,0x18,0x30,0,0,0},
    {0,0x0c,0x18,0x30,0x60,0x30,0x18,0x0c,0,0,0,0},
    {0,0,0,0x7e,0,0x7e,0,0,0,0,0,0},
    {0,0x30,0x18,0x0c,0x06,0x0c,0x18,0x30,0,0,0,0},
    {0,0x3c,0x66,0x0c,0x18,0x18,0,0x18,0,0,0,0},
    {0,0x3c,0x66,0x6e,0x6e,0x60,0x62,0x3c,0,0,0,0},
    {0,0x18,0x3c,0x66,0x7e,0x66,0x66,0x66,0,0,0,0},
    {0,0x7c,0x66,0x66,0x7c,0x66,0x66,0x7c,0,0,0,0},
    {0,0x3c,0x66,0x60,0x60,0x60,0x66,0x3c,0,0,0,0},
    {0,0x78,0x6c,0x66,0x66,0x66,0x6c,0x78,0,0,0,0},
    {0,0x7e,0x60,0x60,0x7c,0x60,0x60,0x7e,0,0,0,0},
    {0,0x7e,0x60,0x60,0x7c,0x60,0x60,0x60,0,0,0,0},
    {0,0x3c,0x66,0x60,0x6e,0x66,0x66,0x3e,0,0,0,0},
    {0,0x66,0x66,0x66,0x7e,0x66,0x66,0x66,0,0,0,0},
    {0,0x3c,0x18,0x18,0x18,0x18,0x18,0x3c,0,0,0,0},
    {0,0x0e,0x06,0x06,0x06,0x66,0x66,0x3c,0,0,0,0},
    {0,0x66,0x6c,0x78,0x70,0x78,0x6c,0x66,0,0,0,0},
    {0,0x60,0x60,0x60,0x60,0x60,0x60,0x7e,0,0,0,0},
    {0,0x63,0x77,0x7f,0x6b,0x63,0x63,0x63,0,0,0,0},
    {0,0x66,0x76,0x7e,0x7e,0x6e,0x66,0x66,0,0,0,0},
    {0,0x3c,0x66,0x66,0x66,0x66,0x66,0x3c,0,0,0,0},
    {0,0x7c,0x66,0x66,0x7c,0x60,0x60,0x60,0,0,0,0},
    {0,0x3c,0x66,0x66,0x66,0x66,0x6c,0x36,0,0,0,0},
    {0,0x7c,0x66,0x66,0x7c,0x78,0x6c,0x66,0,0,0,0},
    {0,0x3c,0x66,0x60,0x3c,0x06,0x66,0x3c,0,0,0,0},
    {0,0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0,0,0,0},
    {0,0x66,0x66,0x66,0x66,0x66,0x66,0x3c,0,0,0,0},
    {0,0x66,0x66,0x66,0x66,0x66,0x3c,0x18,0,0,0,0},
    {0,0x63,0x63,0x63,0x6b,0x7f,0x77,0x63,0,0,0,0},
    {0,0x66,0x66,0x3c,0x18,0x3c,0x66,0x66,0,0,0,0},
    {0,0x66,0x66,0x66,0x3c,0x18,0x18,0x18,0,0,0,0},
    {0,0x7e,0x06,0x0c,0x18,0x30,0x60,0x7e,0,0,0,0},
    {0,0x3c,0x30,0x30,0x30,0x30,0x30,0x3c,0,0,0,0},
    {0,0x40,0x60,0x30,0x18,0x0c,0x06,0x02,0,0,0,0},
    {0,0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0,0,0,0},
    {0,0x18,0x3c,0x66,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0xff,0,0},
    {0,0x30,0x18,0x0c,0,0,0,0,0,0,0,0},
    {0,0,0x3c,0x06,0x3e,0x66,0x66,0x3e,0,0,0,0},
    {0,0x60,0x60,0x7c,0x66,0x66,0x66,0x7c,0,0,0,0},
    {0,0,0x3c,0x66,0x60,0x60,0x66,0x3c,0,0,0,0},
    {0,0x06,0x06,0x3e,0x66,0x66,0x66,0x3e,0,0,0,0},
    {0,0,0x3c,0x66,0x7e,0x60,0x66,0x3c,0,0,0,0},
    {0,0x1c,0x30,0x7c,0x30,0x30,0x30,0x30,0,0,0,0},
    {0,0,0x3e,0x66,0x66,0x3e,0x06,0x3c,0,0,0,0},
    {0,0x60,0x60,0x7c,0x66,0x66,0x66,0x66,0,0,0,0},
    {0,0x18,0,0x38,0x18,0x18,0x18,0x3c,0,0,0,0},
    {0,0x06,0,0x06,0x06,0x06,0x66,0x3c,0,0,0,0},
    {0,0x60,0x60,0x66,0x6c,0x78,0x6c,0x66,0,0,0,0},
    {0,0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0,0,0,0},
    {0,0,0x66,0x7f,0x7f,0x6b,0x63,0x63,0,0,0,0},
    {0,0,0x7c,0x66,0x66,0x66,0x66,0x66,0,0,0,0},
    {0,0,0x3c,0x66,0x66,0x66,0x66,0x3c,0,0,0,0},
    {0,0,0x7c,0x66,0x66,0x7c,0x60,0x60,0,0,0,0},
    {0,0,0x3e,0x66,0x66,0x3e,0x06,0x06,0,0,0,0},
    {0,0,0x7c,0x66,0x60,0x60,0x60,0x60,0,0,0,0},
    {0,0,0x3e,0x60,0x3c,0x06,0x66,0x3c,0,0,0,0},
    {0,0x30,0x7c,0x30,0x30,0x30,0x34,0x18,0,0,0,0},
    {0,0,0x66,0x66,0x66,0x66,0x66,0x3e,0,0,0,0},
    {0,0,0x66,0x66,0x66,0x66,0x3c,0x18,0,0,0,0},
    {0,0,0x63,0x63,0x6b,0x7f,0x77,0x63,0,0,0,0},
    {0,0,0x66,0x3c,0x18,0x3c,0x66,0x66,0,0,0,0},
    {0,0,0x66,0x66,0x66,0x3e,0x06,0x3c,0,0,0,0},
    {0,0,0x7e,0x0c,0x18,0x30,0x60,0x7e,0,0,0,0},
    {0,0x0e,0x18,0x18,0x70,0x18,0x18,0x0e,0,0,0,0},
    {0,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0,0,0,0},
    {0,0x70,0x18,0x18,0x0e,0x18,0x18,0x70,0,0,0,0},
    {0,0x36,0x5c,0,0,0,0,0,0,0,0,0}
};

/* ------------------------------------------------------------------ */
/* Decodificação UTF-8 e normalização de acentos para ASCII           */
/* ------------------------------------------------------------------ */

static uint32_t next_utf8(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t c = *s;
    if (!c) return 0;
    if (c < 0x80)                                 { (*p)++;      return c; }
    if ((c & 0xe0) == 0xc0 && s[1])              { *p += 2;     return ((c & 0x1f) << 6) | (s[1] & 0x3f); }
    if ((c & 0xf0) == 0xe0 && s[1] && s[2])      { *p += 3;     return ((c & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f); }
    (*p)++;
    return c;
}

static char normalize_char(uint32_t cp)
{
    switch (cp) {
    case 0x00c1: case 0x00c0: case 0x00c2: case 0x00c3: case 0x00c4: return 'A';
    case 0x00e1: case 0x00e0: case 0x00e2: case 0x00e3: case 0x00e4: return 'a';
    case 0x00c9: case 0x00c8: case 0x00ca: case 0x00cb:               return 'E';
    case 0x00e9: case 0x00e8: case 0x00ea: case 0x00eb:               return 'e';
    case 0x00cd: case 0x00cc: case 0x00ce: case 0x00cf:               return 'I';
    case 0x00ed: case 0x00ec: case 0x00ee: case 0x00ef:               return 'i';
    case 0x00d3: case 0x00d2: case 0x00d4: case 0x00d5: case 0x00d6: return 'O';
    case 0x00f3: case 0x00f2: case 0x00f4: case 0x00f5: case 0x00f6: return 'o';
    case 0x00da: case 0x00d9: case 0x00db: case 0x00dc:               return 'U';
    case 0x00fa: case 0x00f9: case 0x00fb: case 0x00fc:               return 'u';
    case 0x00c7: return 'C';
    case 0x00e7: return 'c';
    default:     return (cp >= 32 && cp <= 126) ? (char)cp : ' ';
    }
}

/* ------------------------------------------------------------------ */
/* Texto                                                               */
/* ------------------------------------------------------------------ */

int gfx_text_width(const char *text, int scale)
{
    int char_w = scale == 1 ? 16 : 8;
    int len = 0;
    const char *p = text;
    if (!text) return 0;
    while (*p) { next_utf8(&p); ++len; }
    return len * char_w;
}

void canvas_text(Canvas *c, int x, int y, const char *text,
                 uint32_t color, int scale)
{
    const char *p  = text;
    int cur_x      = x;
    int mult       = scale == 1 ? 2 : 1;
    int char_w     = scale == 1 ? 16 : 8;
    int glyph_h    = scale == 1 ? 24 : 12;
    /* dirty rect da string inteira */
    int str_x1 = x;

    if (!c || !c->px || !text) return;

    while (*p) {
        uint32_t cp = next_utf8(&p);
        char ch = normalize_char(cp);
        if (ch >= 32 && ch <= 126) {
            const uint8_t *glyph = FONT_8X12[ch - 32];
            int r, b;
            for (r = 0; r < 12; ++r) {
                uint8_t bits = glyph[r];
                for (b = 0; b < 8; ++b) {
                    if (bits & (0x80 >> b)) {
                        int px_x = cur_x + b * mult;
                        int px_y = y + r * mult;
                        if (scale == 1) {
                            /* usa canvas_rect para 2×2 — mas sem
                               expandir o dirty a cada pixel */
                            int rx0 = px_x < 0 ? 0 : px_x;
                            int ry0 = px_y < 0 ? 0 : px_y;
                            int rx1 = px_x + 2 > c->w ? c->w : px_x + 2;
                            int ry1 = px_y + 2 > c->h ? c->h : px_y + 2;
                            int rr, rc;
                            for (rr = ry0; rr < ry1; ++rr) {
                                uint32_t *line = c->px + rr * c->w;
                                for (rc = rx0; rc < rx1; ++rc)
                                    line[rc] = color;
                            }
                        } else {
                            if (px_x >= 0 && px_x < c->w &&
                                px_y >= 0 && px_y < c->h)
                                c->px[px_y * c->w + px_x] = color;
                        }
                    }
                }
            }
        }
        cur_x += char_w;
    }

    str_x1 = cur_x;
    /* Expande dirty para a bounding box completa da string */
    {
        int bx0 = x     < 0    ? 0    : x;
        int by0 = y     < 0    ? 0    : y;
        int bx1 = str_x1 > c->w ? c->w : str_x1;
        int by1 = y + glyph_h > c->h ? c->h : y + glyph_h;
        mark_dirty(c, bx0, by0, bx1, by1);
    }
}

/* ------------------------------------------------------------------ */
/* Word wrap                                                           */
/* ------------------------------------------------------------------ */

int gfx_wrap(const char *text, int maxw, int scale,
             char out[][64], int max_lines)
{
    int char_w    = scale == 1 ? 16 : 8;
    int line_cap  = maxw / (char_w > 0 ? char_w : 8);
    int num_lines = 0;
    const char *p = text;

    if (!text || max_lines <= 0) return 0;
    if (line_cap < 1) line_cap = 1;
    if (line_cap > 63) line_cap = 63;

    while (*p && num_lines < max_lines) {
        const char *line_start, *word_end, *next_scan;
        int line_chars = 0, copy_len = 0;

        while (*p == ' ' || *p == '\t' || *p == '\n') {
            if (*p == '\n') {
                ++p;
                out[num_lines][0] = '\0';
                if (++num_lines >= max_lines) return num_lines;
            } else { ++p; }
        }
        if (!*p) break;

        line_start = p;
        word_end   = p;
        next_scan  = p;

        while (*next_scan && *next_scan != '\n') {
            const char *saved = next_scan;
            next_utf8(&next_scan);
            ++line_chars;
            if (*saved == ' ' || *saved == '\t')
                word_end = saved;
            if (line_chars >= line_cap) {
                if (word_end > line_start) {
                    copy_len = (int)(word_end - line_start);
                    p = word_end;
                    while (*p == ' ' || *p == '\t') ++p;
                } else {
                    copy_len = (int)(next_scan - line_start);
                    p = next_scan;
                }
                break;
            }
        }

        if (copy_len == 0) {
            copy_len = (int)(next_scan - line_start);
            p = next_scan;
            if (*p == '\n') ++p;
        }
        if (copy_len > 63) copy_len = 63;
        memcpy(out[num_lines], line_start, (size_t)copy_len);
        out[num_lines][copy_len] = '\0';
        ++num_lines;
    }
    return num_lines;
}
