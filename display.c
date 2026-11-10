/*
 * display.c — Janela, eventos e desenho da Pestinha via SDL2.
 *
 * Sem XCB, sem X11, sem xutil.
 * Funciona em Linux (X11 e Wayland), Windows e macOS.
 *
 * Todo o código de desenho (px, rect, ellipse, draw_face…) é puro C
 * e foi copiado sem alteração de display_xcb.c.
 * Apenas a infraestrutura de janela/eventos/blit foi trocada por SDL2.
 *
 * GPL-3.0-or-later
 */
#include "display.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Constantes de resolução interna (inalteradas)                      */
/* ------------------------------------------------------------------ */

#define CW    320
#define CH    200
#define SCALE   3
#define W    (CW * SCALE)   /* 960 */
#define H    (CH * SCALE)   /* 600 */
#define WW    100
#define WH    100
#define ZOOM  2.0

/* ------------------------------------------------------------------ */
/* Struct PestinhaDisplay — SDL2                                       */
/* ------------------------------------------------------------------ */

struct PestinhaDisplay {
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *tex;      /* streaming ARGB8888, tamanho w×h       */

    uint32_t     *pixels;   /* buffer interno alta-res W×H           */
    uint32_t     *present;  /* buffer de saída reduzido w×h          */

    int           w, h;
    int           is_desktop;

    /* Paleta de cores */
    uint32_t      frame, shell, screen_color, eye, shine, tie;

    /* Fila de eventos de mouse para display_poll_mouse */
    DisplayMouse  mouse_queue[16];
    int           mouse_head, mouse_tail;
};

/*
 * DispWindow — janela secundária SDL2 (menu, dialogo, fx).
 * Cada uma tem seu próprio renderer e textura, e uma fila de eventos.
 */
struct DispWindow {
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *tex;
    uint32_t     *present;   /* buffer w×h ARGB8888 */
    int           w, h;
    int           visible;
    int           needs_redraw;

    /* Fila de eventos de mouse */
    DisplayMouse  mouse_queue[16];
    int           mouse_head, mouse_tail;
};

/* Ponteiro global usado pelas primitivas de desenho durante um frame */
static uint32_t *canvas;

/* ------------------------------------------------------------------ */
/* Primitivas de desenho (puras C, sem plataforma)                   */
/* ------------------------------------------------------------------ */

static uint32_t rgb(int r, int g, int b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t hex(unsigned int v)
{
    return ((v >> 16) & 255u) << 16 | ((v >> 8) & 255u) << 8 | (v & 255u);
}

static void px(int x, int y, uint32_t c)
{
    if (x >= 0 && x < W && y >= 0 && y < H)
        canvas[y * W + x] = c;
}

static void rect(int x, int y, int w, int h, uint32_t c)
{
    int i, j;
    for (j = y * SCALE; j < (y + h) * SCALE; j++)
        for (i = x * SCALE; i < (x + w) * SCALE; i++)
            px(i, j, c);
}

static void ellipse(int cx, int cy, int rx, int ry, uint32_t c)
{
    int x, y;
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    cx *= SCALE; cy *= SCALE; rx *= SCALE; ry *= SCALE;
    for (y = -ry; y <= ry; y++)
        for (x = -rx; x <= rx; x++)
            if (x * x * ry * ry + y * y * rx * rx <= rx * rx * ry * ry)
                px(cx + x, cy + y, c);
}

static void rounded_rect(int x, int y, int w, int h, int r, uint32_t c)
{
    rect(x + r, y,         w - 2 * r, h,         c);
    rect(x,     y + r,     w,         h - 2 * r, c);
    ellipse(x + r,         y + r,         r, r, c);
    ellipse(x + w - r - 1, y + r,         r, r, c);
    ellipse(x + r,         y + h - r - 1, r, r, c);
    ellipse(x + w - r - 1, y + h - r - 1, r, r, c);
}

static void line(int x0, int y0, int x1, int y1, uint32_t c, int thick)
{
    int dx, dy, sx, sy, err, r;
    x0 *= SCALE; y0 *= SCALE; x1 *= SCALE; y1 *= SCALE;
    dx = abs(x1 - x0); sx = x0 < x1 ?  1 : -1;
    dy = -abs(y1 - y0); sy = y0 < y1 ?  1 : -1;
    err = dx + dy;
    r = thick * SCALE / 2;
    for (;;) {
        int xx, yy;
        for (yy = -r; yy <= r; yy++)
            for (xx = -r; xx <= r; xx++)
                if (xx * xx + yy * yy <= r * r)
                    px(x0 + xx, y0 + yy, c);
        if (x0 == x1 && y0 == y1) break;
        { int e = 2 * err;
          if (e >= dy) { err += dy; x0 += sx; }
          if (e <= dx) { err += dx; y0 += sy; } }
    }
}

static void polygon(const int *x, const int *y, int n, uint32_t c)
{
    int yy, miny = H, maxy = 0, i;
    for (i = 0; i < n; i++) {
        if (y[i] * SCALE < miny) miny = y[i] * SCALE;
        if (y[i] * SCALE > maxy) maxy = y[i] * SCALE;
    }
    if (miny < 0) miny = 0;
    if (maxy >= H) maxy = H - 1;
    for (yy = miny; yy <= maxy; yy++) {
        int nodes[8], nn = 0, j, k;
        for (i = 0, j = n - 1; i < n; j = i++) {
            int yi = y[i] * SCALE, yj = y[j] * SCALE;
            if ((yi < yy && yj >= yy) || (yj < yy && yi >= yy))
                nodes[nn++] = x[i] * SCALE +
                    (yy - yi) * (x[j] - x[i]) / (yj - yi);
        }
        for (i = 0; i < nn; i++)
            for (k = i + 1; k < nn; k++)
                if (nodes[i] > nodes[k]) {
                    int t = nodes[i]; nodes[i] = nodes[k]; nodes[k] = t;
                }
        for (i = 0; i + 1 < nn; i += 2)
            for (k = nodes[i]; k <= nodes[i + 1]; k++)
                px(k, yy, c);
    }
}

static int inside_roundrect(int px0, int py0, int x, int y, int w, int h, int r)
{
    int cx = px0 < x + r ? x + r : px0 >= x + w - r ? x + w - r - 1 : px0;
    int cy = py0 < y + r ? y + r : py0 >= y + h - r ? y + h - r - 1 : py0;
    int dx = px0 - cx, dy = py0 - cy;
    return dx * dx + dy * dy <= r * r;
}

static int inside_polygon(int px0, int py0, const int *x, const int *y, int n)
{
    int i, j, c = 0;
    for (i = 0, j = n - 1; i < n; j = i++) {
        if (((y[i] > py0) != (y[j] > py0)) &&
            (px0 < (x[j] - x[i]) * (py0 - y[i]) / (double)(y[j] - y[i]) + x[i]))
            c = !c;
    }
    return c;
}

/* ------------------------------------------------------------------ */
/* Formas dos olhos e desenho do rosto                                */
/* ------------------------------------------------------------------ */

typedef struct { double w, h, inner, outer, smile, dy; } EyeShape;

static const EyeShape shapes[12] = {
    {30, 40, .14, 0,   0,   0},  {32, 34, 0,   0,   .36, 1},
    {34, 36, 0,   0,   .55, -2}, {30, 46, 0,   .08, 0,   -2},
    {34, 36, .55, 0,   0,   2},  {28, 34, 0,   .5,  0,   7},
    {26, 50, 0,   0,   0,   -3}, {38, 52, 0,   .22, 0,   0},
    {32, 4,  0,   0,   0,   8},  {32, 34, 0,   0,   .55, -2},
    {34, 36, .55, 0,   0,   2},  {26, 50, 0,   0,   0,   -3}
};

static void draw_eye(double cx, double cy, const EyeShape *s, int side,
                     double gaze_x, double gaze_y, double blink,
                     const PestinhaDisplay *d)
{
    double w  = s->w * ZOOM;
    double h  = fmax(3.0, s->h * blink) * ZOOM;
    double top = cy - h / 2.0;
    int x  = (int)round(cx - w / 2);
    int y  = (int)round(top);
    int iw = (int)round(w);
    int ih = (int)round(h);
    int r  = (int)round(fmin(w, h) * .38);

    rounded_rect(x, y, iw, ih, r, d->eye);

    if (blink > .5) {
        double outer_x = cx + side * w / 2;
        double inner_x = cx - side * w / 2;
        int ox = (int)round(outer_x + side * 8);
        int ix = (int)round(inner_x - side * 8);
        int pts_x[4] = {
            (int)round(ox * SCALE), (int)round(ix * SCALE),
            (int)round((inner_x - side * 8) * SCALE),
            (int)round((outer_x + side * 8) * SCALE)
        };
        int pts_y[4] = {
            (int)round((top - 80) * SCALE), (int)round((top - 80) * SCALE),
            (int)round((top + s->inner * h) * SCALE),
            (int)round((top + s->outer * h) * SCALE)
        };
        {
            int yy, xx;
            int xh = x * SCALE, yh = y * SCALE;
            int wh = iw * SCALE, hh = ih * SCALE, rh = r * SCALE;
            int y0 = yh < 0 ? 0 : yh;
            int y1 = yh + hh >= H ? H - 1 : yh + hh;
            for (yy = y0; yy <= y1; yy++)
                for (xx = xh; xx < xh + wh; xx++)
                    if (inside_roundrect(xx, yy, xh, yh, wh, hh, rh) &&
                        inside_polygon(xx, yy, pts_x, pts_y, 4))
                        px(xx, yy, d->screen_color);
        }
        if (s->smile > .02) {
            int ch = (int)round(h * s->smile * 1.6);
            int ex = (int)round(w * .75) * SCALE;
            int ey = (int)round((top + h - ch * .45 + ch * .5) * SCALE);
            int yy, xx;
            int xh = x * SCALE, yh = y * SCALE;
            int wh = iw * SCALE, hh = ih * SCALE, rh = r * SCALE;
            double rhs;
            ch  *= SCALE;
            rhs  = (double)ex * ex * ch * ch;
            for (yy = yh < 0 ? 0 : yh; yy <= yh + hh && yy < H; yy++)
                for (xx = xh; xx < xh + wh; xx++) {
                    double dx = xx - cx * SCALE, dy = yy - ey;
                    if (inside_roundrect(xx, yy, xh, yh, wh, hh, rh) &&
                        (dx * dx * ch * ch + dy * dy * ex * ex <= rhs))
                        px(xx, yy, d->screen_color);
                }
        }
    }

    if (blink > .5 && h >= 8) {
        int sx = (int)round(cx - w * .18 + gaze_x * 3);
        int sy = (int)round(top + h * (.24 + fmax(s->inner, s->outer) * .6)
                             + gaze_y * 2);
        ellipse(sx, sy, 7, 7, d->shine);
    }
}

static void draw_face(const PestinhaDisplay *d, int face, double elapsed)
{
    const double cx = 160, cy = 99;
    EyeShape s = shapes[face < 0 || face > 11 ? 0 : face];
    double blink = 1.0;
    double gx = sin(elapsed * 1.1), gy = sin(elapsed * .7) * .3;
    int side;
    int blinking = fmod(elapsed, 3.7) > .06 && fmod(elapsed, 3.7) < .20;

    rounded_rect(40,  10, 240, 180, 40, d->frame);
    rounded_rect(50,  20, 220, 160, 30, d->shell);
    rounded_rect(62,  32, 196, 136, 24, d->screen_color);

    if (face == 9) {
        for (side = -1; side <= 1; side += 2) {
            int ex = (int)(cx + side * 48), ey = (int)(cy + 2), j;
            for (j = 0; j < 56; j++) {
                double u = (double)j / 55.0;
                int xi = ex - 28 + j;
                int yi = ey - (int)round(12 * sin(u * 3.141592653589793));
                ellipse(xi, yi, 5, 5, d->eye);
            }
        }
    } else {
        if (blinking && face != 8) blink = .08;
        for (side = -1; side <= 1; side += 2) {
            double h = s.h;
            if (face == 3 && side == 1) h *= 1.15;
            draw_eye(cx + side * 48 + gx * 8, cy + s.dy * 2 + gy * 6,
                     &(EyeShape){s.w, h, s.inner, s.outer, s.smile, s.dy},
                     side, gx, gy, blink, d);
        }
    }
    if (face == 8) {
        for (side = -1; side <= 1; side += 2)
            line((int)(cx + side * 48 - 31), (int)(cy + 16),
                 (int)(cx + side * 48 + 31), (int)(cy + 16), d->eye, 7);
    }

    if (d->tie) {
        int tx[] = {153, 167, 166, 154}, ty[] = {142, 142, 154, 154};
        polygon(tx, ty, 4, d->tie);
        {
            int bx[] = {154, 166, 171, 160, 149};
            int by[] = {153, 153, 176, 184, 176};
            polygon(bx, by, 5, d->tie);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Paleta de cores                                                     */
/* ------------------------------------------------------------------ */

static void setup_colors(PestinhaDisplay *d, int jorge)
{
    if (jorge) {
        d->frame        = hex(0xe2b13c);
        d->shell        = hex(0x14120c);
        d->screen_color = hex(0x1c1a12);
        d->eye          = hex(0xffd166);
        d->shine        = hex(0xfff3c4);
        d->tie          = hex(0xb3261e);
    } else {
        d->frame        = hex(0x63e56f);
        d->shell        = hex(0x101512);
        d->screen_color = hex(0x171e19);
        d->eye          = hex(0x3ee877);
        d->shine        = hex(0xc8ffd8);
        d->tie          = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Helpers internos SDL2                                               */
/* ------------------------------------------------------------------ */

static SDL_Texture *make_texture(SDL_Renderer *ren, int w, int h)
{
    return SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                             SDL_TEXTUREACCESS_STREAMING, w, h);
}

static SDL_Renderer *make_renderer(SDL_Window *win)
{
    SDL_Renderer *r = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    return r;
}

static void destroy_display(PestinhaDisplay *d)
{
    if (!d) return;
    if (d->tex) SDL_DestroyTexture(d->tex);
    if (d->ren) SDL_DestroyRenderer(d->ren);
    if (d->win) SDL_DestroyWindow(d->win);
    free(d->pixels);
    free(d->present);
    free(d);
    canvas = NULL;
}

/* ------------------------------------------------------------------ */
/* display_open — janela normal 100×100                               */
/* ------------------------------------------------------------------ */

PestinhaDisplay *display_open(int jorge)
{
    PestinhaDisplay *d = calloc(1, sizeof *d);
    if (!d) return NULL;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        free(d);
        return NULL;
    }

    d->w = WW; d->h = WH; d->is_desktop = 0;

    d->win = SDL_CreateWindow("Pestinha",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        d->w, d->h, SDL_WINDOW_SHOWN);
    if (!d->win) goto fail;

    d->ren = make_renderer(d->win);
    if (!d->ren) goto fail;

    d->tex = make_texture(d->ren, d->w, d->h);
    if (!d->tex) goto fail;

    d->pixels  = calloc((size_t)W * H, sizeof(uint32_t));
    d->present = calloc((size_t)d->w * d->h, sizeof(uint32_t));
    if (!d->pixels || !d->present) goto fail;

    setup_colors(d, jorge);
    canvas = d->pixels;
    return d;

fail:
    destroy_display(d);
    SDL_Quit();
    return NULL;
}

/* ------------------------------------------------------------------ */
/* display_open_desktop — flutuante sem borda                         */
/* ------------------------------------------------------------------ */

PestinhaDisplay *display_open_desktop(int jorge, int level)
{
    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS;
    PestinhaDisplay *d = calloc(1, sizeof *d);
    if (!d) return NULL;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        free(d);
        return NULL;
    }

    d->is_desktop = 1;
    if      (level == 0) { d->w =  90; d->h =  68; }
    else if (level == 2) { d->w = 160; d->h = 120; }
    else                 { d->w = 120; d->h =  90; }

/* SDL_WINDOW_ALWAYS_ON_TOP introduzido em SDL 2.0.5 */
#if SDL_VERSION_ATLEAST(2, 0, 5)
    flags |= SDL_WINDOW_ALWAYS_ON_TOP;
#endif

    d->win = SDL_CreateWindow("Pestinha",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        d->w, d->h, flags);
    if (!d->win) goto fail;

    d->ren = make_renderer(d->win);
    if (!d->ren) goto fail;

    d->tex = make_texture(d->ren, d->w, d->h);
    if (!d->tex) goto fail;

    d->pixels  = calloc((size_t)W * H, sizeof(uint32_t));
    d->present = calloc((size_t)d->w * d->h, sizeof(uint32_t));
    if (!d->pixels || !d->present) goto fail;

    setup_colors(d, jorge);
    canvas = d->pixels;
    return d;

fail:
    destroy_display(d);
    SDL_Quit();
    return NULL;
}

/* ------------------------------------------------------------------ */
/* display_close                                                       */
/* ------------------------------------------------------------------ */

void display_close(PestinhaDisplay *d)
{
    destroy_display(d);
    SDL_Quit();
}

/* ------------------------------------------------------------------ */
/* display_poll — teclas + enfileira eventos de mouse                  */
/* ------------------------------------------------------------------ */

static void enqueue_mouse(PestinhaDisplay *d, int type, int button,
                          int root_x, int root_y, int win_x, int win_y)
{
    int next = (d->mouse_head + 1) % 16;
    if (next == d->mouse_tail) return;   /* fila cheia */
    d->mouse_queue[d->mouse_head].type     = type;
    d->mouse_queue[d->mouse_head].button   = button;
    d->mouse_queue[d->mouse_head].root_x   = root_x;
    d->mouse_queue[d->mouse_head].root_y   = root_y;
    d->mouse_queue[d->mouse_head].win_x    = win_x;
    d->mouse_queue[d->mouse_head].win_y    = win_y;
    d->mouse_head = next;
}

int display_poll(PestinhaDisplay *d)
{
    SDL_Event ev;
    if (!d) return -1;

    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {

        case SDL_QUIT:
            return -1;

        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE)
                return -1;
            break;

        case SDL_KEYDOWN:
            switch (ev.key.keysym.scancode) {
            case SDL_SCANCODE_Q: return 'q';
            case SDL_SCANCODE_W: return 'w';
            case SDL_SCANCODE_T: return 't';
            case SDL_SCANCODE_P: return 'p';
            case SDL_SCANCODE_A: return 'a';
            case SDL_SCANCODE_F: return 'f';
            case SDL_SCANCODE_L: return 'l';
            case SDL_SCANCODE_X: return 'x';
            case SDL_SCANCODE_C: return 'c';
            case SDL_SCANCODE_V: return 'v';
            case SDL_SCANCODE_M: return 'm';
            default: break;
            }
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        {
            int button = ev.button.button == SDL_BUTTON_LEFT   ? 1
                       : ev.button.button == SDL_BUTTON_RIGHT  ? 3
                       : ev.button.button == SDL_BUTTON_MIDDLE ? 2 : 0;
            int type   = ev.type == SDL_MOUSEBUTTONDOWN
                       ? DISPLAY_EV_BUTTON_PRESS : DISPLAY_EV_BUTTON_RELEASE;
            int rx, ry;
            SDL_GetGlobalMouseState(&rx, &ry);
            enqueue_mouse(d, type, button, rx, ry,
                          ev.button.x, ev.button.y);
            break;
        }

        case SDL_MOUSEMOTION:
        {
            int rx, ry;
            SDL_GetGlobalMouseState(&rx, &ry);
            enqueue_mouse(d, DISPLAY_EV_MOTION, 0, rx, ry,
                          ev motion.x, ev.motion.y);
            break;
        }

        default:
            break;
        }
    }
    return 0;
}

int display_poll_mouse(PestinhaDisplay *d, DisplayMouse *m)
{
    if (!d || d->mouse_head == d->mouse_tail) return 0;
    *m = d->mouse_queue[d->mouse_tail];
    d->mouse_tail = (d->mouse_tail + 1) % 16;
    return 1;
}

/* ------------------------------------------------------------------ */
/* display_size / display_screen / display_work_area / display_move   */
/* ------------------------------------------------------------------ */

void display_size(PestinhaDisplay *d, int *w, int *h)
{
    if (!d) return;
    if (w) *w = d->w;
    if (h) *h = d->h;
}

void display_screen(PestinhaDisplay *d, int *sw, int *sh)
{
    SDL_DisplayMode dm;
    (void)d;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
        if (sw) *sw = dm.w;
        if (sh) *sh = dm.h;
    } else {
        if (sw) *sw = 1920;
        if (sh) *sh = 1080;
    }
}

void display_work_area(PestinhaDisplay *d, Area *wa)
{
    SDL_Rect r;
    (void)d;
    if (!wa) return;
    if (SDL_GetDisplayUsableBounds(0, &r) == 0) {
        wa->x = r.x; wa->y = r.y;
        wa->w = r.w; wa->h = r.h;
    } else {
        wa->x = 0; wa->y = 0;
        display_screen(d, &wa->w, &wa->h);
    }
}

void display_move(PestinhaDisplay *d, int x, int y)
{
    if (d && d->win)
        SDL_SetWindowPosition(d->win, x, y);
}

/* ------------------------------------------------------------------ */
/* display_platforms — stub (SDL2 não enumera janelas de terceiros)   */
/* ------------------------------------------------------------------ */

int display_platforms(PestinhaDisplay *d, int min_top, Platform *out, int max)
{
    (void)d; (void)min_top; (void)out; (void)max;
    return 0;
}

/* ------------------------------------------------------------------ */
/* display_draw                                                        */
/* ------------------------------------------------------------------ */

void display_draw(PestinhaDisplay *d, int face, double energy,
                  double cheer, double anger, double sadness, int corner,
                  int sleeping, double elapsed)
{
    int x, y, ox, oy;
    (void)energy; (void)cheer; (void)anger; (void)sadness;
    (void)corner; (void)sleeping;

    if (!d || !d->win) return;

    /* Desenha no buffer interno W×H */
    canvas = d->pixels;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            px(x, y, rgb(58, 63, 71));

    draw_face(d, face, elapsed);

    /* Reduz para o buffer de saída d->w × d->h */
    if (d->is_desktop) {
        double step_x = (double)(W - 1) / (double)d->w;
        double step_y = (double)(H - 1) / (double)d->h;
        for (oy = 0; oy < d->h; oy++) {
            int src_y = (int)(oy * step_y);
            for (ox = 0; ox < d->w; ox++) {
                int src_x = (int)(ox * step_x);
                d->present[oy * d->w + ox] = d->pixels[src_y * W + src_x];
            }
        }
    } else {
        /* Modo 100×100: fundo + área 80×60 centralizada com downsample 9×9 */
        for (y = 0; y < WH; y++)
            for (x = 0; x < WW; x++)
                d->present[y * WW + x] = rgb(58, 63, 71);

        for (oy = 0; oy < 60; oy++)
            for (ox = 0; ox < 80; ox++) {
                unsigned int rr = 0, gg = 0, bb = 0;
                unsigned int sx, sy;
                for (sy = 0; sy < 9; sy++)
                    for (sx = 0; sx < 9; sx++) {
                        uint32_t p = d->pixels[(30 + oy * 9 + sy) * W
                                               + 120 + ox * 9 + sx];
                        rr += (p >> 16) & 255u;
                        gg += (p >>  8) & 255u;
                        bb +=  p        & 255u;
                    }
                d->present[(20 + oy) * WW + 10 + ox] =
                    rgb(rr / 81, gg / 81, bb / 81);
            }
    }

    /* Blit para a janela via SDL2 */
    SDL_UpdateTexture(d->tex, NULL, d->present,
                      d->w * (int)sizeof(uint32_t));
    SDL_RenderClear(d->ren);
    SDL_RenderCopy(d->ren, d->tex, NULL, NULL);
    SDL_RenderPresent(d->ren);
}

/* ------------------------------------------------------------------ */
/* DispWindow — janelas secundárias (menu, dialogo, fx)                */
/* ------------------------------------------------------------------ */

DispWindow *disp_window_open(int sx, int sy, int w, int h)
{
    DispWindow *dw = calloc(1, sizeof *dw);
    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP;
    if (!dw) return NULL;

#if SDL_VERSION_ATLEAST(2, 0, 5)
    flags |= SDL_WINDOW_ALWAYS_ON_TOP;
#endif

    dw->win = SDL_CreateWindow("Pestinha-Disp", sx, sy, w, h, flags);
    if (!dw->win) { free(dw); return NULL; }

    dw->ren = SDL_CreateRenderer(dw->win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!dw->ren) { SDL_DestroyWindow(dw->win); free(dw); return NULL; }

    dw->tex = SDL_CreateTexture(dw->ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!dw->tex) { SDL_DestroyRenderer(dw->ren); SDL_DestroyWindow(dw->win); free(dw); return NULL; }

    dw->present = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    dw->w = w; dw->h = h; dw->visible = 1;
    return dw;
}

void disp_window_close(DispWindow *dw)
{
    if (!dw) return;
    if (dw->tex) SDL_DestroyTexture(dw->tex);
    if (dw->ren) SDL_DestroyRenderer(dw->ren);
    if (dw->win) SDL_DestroyWindow(dw->win);
    free(dw->present);
    free(dw);
}

void disp_window_move(DispWindow *dw, int x, int y)
{
    if (dw && dw->win) SDL_SetWindowPosition(dw->win, x, y);
}

void disp_window_raise(DispWindow *dw)
{
    if (dw && dw->win) SDL_RaiseWindow(dw->win);
}

void disp_window_set_visible(DispWindow *dw, int visible)
{
    if (!dw || dw->visible == visible) return;
    dw->visible = visible;
    if (visible) SDL_ShowWindow(dw->win);
    else SDL_HideWindow(dw->win);
}

int disp_window_is_visible(DispWindow *dw) { return dw ? dw->visible : 0; }

void disp_window_get_size(DispWindow *dw, int *w, int *h)
{
    if (!dw) return;
    if (w) *w = dw->w;
    if (h) *h = dw->h;
}

static void dw_enqueue(DispWindow *dw, int type, int button,
                       int root_x, int root_y, int win_x, int win_y)
{
    int next = (dw->mouse_head + 1) % 16;
    if (next == dw->mouse_tail) return;
    dw->mouse_queue[dw->mouse_head].type     = type;
    dw->mouse_queue[dw->mouse_head].button   = button;
    dw->mouse_queue[dw->mouse_head].root_x   = root_x;
    dw->mouse_queue[dw->mouse_head].root_y   = root_y;
    dw->mouse_queue[dw->mouse_head].win_x    = win_x;
    dw->mouse_queue[dw->mouse_head].win_y    = win_y;
    dw->mouse_head = next;
}

int disp_window_poll(DispWindow *dw, DisplayMouse *m)
{
    SDL_Event ev;
    if (!dw) return 0;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            dw->visible = 0;
            return -1;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE) { dw->visible = 0; return -1; }
            if (ev.window.event == SDL_WINDOWEVENT_EXPOSE) dw->needs_redraw = 1;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        {
            int button = ev.button.button == SDL_BUTTON_LEFT  ? 1
                       : ev.button.button == SDL_BUTTON_RIGHT ? 3 : 0;
            int type   = ev.type == SDL_MOUSEBUTTONDOWN
                       ? DISPLAY_EV_BUTTON_PRESS : DISPLAY_EV_BUTTON_RELEASE;
            int rx, ry;
            SDL_GetGlobalMouseState(&rx, &ry);
            dw_enqueue(dw, type, button, rx, ry, ev.button.x, ev.button.y);
            break;
        }
        case SDL_MOUSEMOTION:
        {
            int rx, ry;
            SDL_GetGlobalMouseState(&rx, &ry);
            dw_enqueue(dw, DISPLAY_EV_MOTION, 0, rx, ry, ev.motion.x, ev.motion.y);
            break;
        }
        default: break;
        }
    }
    if (dw->mouse_head == dw->mouse_tail) return 0;
    *m = dw->mouse_queue[dw->mouse_tail];
    dw->mouse_tail = (dw->mouse_tail + 1) % 16;
    return 1;
}

void disp_window_invalidate(DispWindow *dw)
{
    if (dw) dw->needs_redraw = 1;
}

void disp_window_put_canvas(DispWindow *dw, const Canvas *cv,
                            int sx, int sy, int sw, int sh, int dx, int dy)
{
    int i, j;
    if (!dw || !dw->present || !cv) return;
    if (sx < 0 || sy < 0 || sw <= 0 || sh <= 0) return;
    if (sx + sw > cv->w || sy + sh > cv->h) return;
    if (dx + sw > dw->w || dy + sh > dw->h) return;
    for (j = 0; j < sh; j++)
        for (i = 0; i < sw; i++)
            dw->present[(dy + j) * dw->w + (dx + i)] =
                cv->px[(sy + j) * cv->w + (sx + i)];
    dw->needs_redraw = 1;
}

static void dw_present(DispWindow *dw)
{
    if (!dw || !dw->needs_redraw || !dw->win) return;
    SDL_UpdateTexture(dw->tex, NULL, dw->present, dw->w * (int)sizeof(uint32_t));
    SDL_RenderClear(dw->ren);
    SDL_RenderCopy(dw->ren, dw->tex, NULL, NULL);
    SDL_RenderPresent(dw->ren);
    dw->needs_redraw = 0;
}

/* ------------------------------------------------------------------ */
/* Lista global de DispWindows abertas + present_all                  */
/* ------------------------------------------------------------------ */

#define MAX_DW 8
static DispWindow *g_dw[MAX_DW];
static int g_ndw = 0;

static void dw_register(DispWindow *dw)
{
    if (g_ndw < MAX_DW) g_dw[g_ndw++] = dw;
}

static void dw_unregister(DispWindow *dw)
{
    int i;
    for (i = 0; i < g_ndw; i++)
        if (g_dw[i] == dw) { g_dw[i] = g_dw[--g_ndw]; return; }
}

/* Chama após display_draw() para blitar todas as janelas secundárias. */
void disp_window_present_all(void)
{
    int i;
    for (i = 0; i < g_ndw; i++)
        if (g_dw[i]) dw_present(g_dw[i]);
}

/* Sobrescreve as funções de abrir/fechar para registrar/des registrar. */
DispWindow *disp_window_open(int sx, int sy, int w, int h);
void disp_window_close(DispWindow *w);

static DispWindow *dw_open_reg(int sx, int sy, int w, int h)
{
    DispWindow *dw = dw_open_raw(sx, sy, w, h);
    if (dw) dw_register(dw);
    return dw;
}

static void dw_close_reg(DispWindow *dw)
{
    if (!dw) return;
    dw_unregister(dw);
    dw_close_raw(dw);
}
