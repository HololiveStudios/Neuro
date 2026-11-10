#include "display_xcb.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

#include "xutil.h"

#define CW 320
#define CH 200
#define SCALE 3
#define W (CW * SCALE)
#define H (CH * SCALE)
#define WW 100
#define WH 100
#define ZOOM 2.0

typedef struct { double w, h, inner, outer, smile, dy; } EyeShape;

struct PestinhaDisplay {
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    xcb_window_t win;
    xcb_gcontext_t gc;
    xcb_atom_t wm_protocols, wm_delete;
    uint32_t *pixels, *present;
    int w, h;
    int is_desktop;
    uint32_t frame, shell, screen_color, eye, shine, tie;
};

static uint32_t *canvas;

static uint32_t rgb(int r, int g, int b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t hex(unsigned int v)
{
    return ((v >> 16) & 255) << 16 | ((v >> 8) & 255) << 8 | (v & 255);
}

static void px(int x, int y, uint32_t c)
{
    if (x >= 0 && x < W && y >= 0 && y < H) canvas[y * W + x] = c;
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
    rect(x + r, y, w - 2 * r, h, c);
    rect(x, y + r, w, h - 2 * r, c);
    ellipse(x + r, y + r, r, r, c);
    ellipse(x + w - r - 1, y + r, r, r, c);
    ellipse(x + r, y + h - r - 1, r, r, c);
    ellipse(x + w - r - 1, y + h - r - 1, r, r, c);
}

static void line(int x0, int y0, int x1, int y1, uint32_t c, int thick)
{
    int dx, dy, sx, sy, err, r;
    x0 *= SCALE; y0 *= SCALE; x1 *= SCALE; y1 *= SCALE;
    dx = abs(x1 - x0); sx = x0 < x1 ? 1 : -1;
    dy = -abs(y1 - y0); sy = y0 < y1 ? 1 : -1;
    err = dx + dy;
    r = thick * SCALE / 2;
    for (;;) {
        int xx, yy;
        for (yy = -r; yy <= r; yy++)
            for (xx = -r; xx <= r; xx++)
                if (xx * xx + yy * yy <= r * r) px(x0 + xx, y0 + yy, c);
        if (x0 == x1 && y0 == y1) break;
        {
            int e = 2 * err;
            if (e >= dy) { err += dy; x0 += sx; }
            if (e <= dx) { err += dx; y0 += sy; }
        }
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
                nodes[nn++] = x[i] * SCALE + (yy - yi) * (x[j] - x[i]) / (yj - yi);
        }
        for (i = 0; i < nn; i++)
            for (k = i + 1; k < nn; k++)
                if (nodes[i] > nodes[k]) {
                    int t = nodes[i];
                    nodes[i] = nodes[k];
                    nodes[k] = t;
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

static const EyeShape shapes[12] = {
    {30, 40, .14, 0, 0, 0}, {32, 34, 0, 0, .36, 1}, {34, 36, 0, 0, .55, -2},
    {30, 46, 0, .08, 0, -2}, {34, 36, .55, 0, 0, 2}, {28, 34, 0, .5, 0, 7},
    {26, 50, 0, 0, 0, -3}, {38, 52, 0, .22, 0, 0}, {32, 4, 0, 0, 0, 8},
    {32, 34, 0, 0, .55, -2}, {34, 36, .55, 0, 0, 2}, {26, 50, 0, 0, 0, -3}
};

static void draw_eye(double cx, double cy, const EyeShape *s, int side,
                     double gaze_x, double gaze_y, double blink,
                     const PestinhaDisplay *d)
{
    double w = s->w * ZOOM, h = fmax(3.0, s->h * blink) * ZOOM, top = cy - h / 2.0;
    int x = (int)round(cx - w / 2), y = (int)round(top), iw = (int)round(w), ih = (int)round(h);
    int r = (int)round(fmin(w, h) * .38);
    rounded_rect(x, y, iw, ih, r, d->eye);
    if (blink > .5) {
        double outer_x = cx + side * w / 2, inner_x = cx - side * w / 2;
        int ox = (int)round(outer_x + side * 8), ix = (int)round(inner_x - side * 8);
        int pts_x[4] = {(int)round(ox * SCALE), (int)round(ix * SCALE),
                        (int)round((inner_x - side * 8) * SCALE), (int)round((outer_x + side * 8) * SCALE)};
        int pts_y[4] = {(int)round((top - 80) * SCALE), (int)round((top - 80) * SCALE),
                        (int)round((top + s->inner * h) * SCALE), (int)round((top + s->outer * h) * SCALE)};
        {
            int yy, xx, xh = x * SCALE, yh = y * SCALE, wh = iw * SCALE, hh = ih * SCALE, rh = r * SCALE;
            int y0 = yh < 0 ? 0 : yh, y1 = yh + hh >= H ? H - 1 : yh + hh;
            for (yy = y0; yy <= y1; yy++)
                for (xx = xh; xx < xh + wh; xx++)
                    if (inside_roundrect(xx, yy, xh, yh, wh, hh, rh) && inside_polygon(xx, yy, pts_x, pts_y, 4))
                        px(xx, yy, d->screen_color);
        }
        if (s->smile > .02) {
            int ch = (int)round(h * s->smile * 1.6), ex = (int)round(w * .75) * SCALE;
            int ey = (int)round((top + h - ch * .45 + ch * .5) * SCALE), yy, xx;
            int xh = x * SCALE, yh = y * SCALE, wh = iw * SCALE, hh = ih * SCALE, rh = r * SCALE;
            double rhs;
            ch *= SCALE; rhs = (double)ex * ex * ch * ch;
            for (yy = yh < 0 ? 0 : yh; yy <= yh + hh && yy < H; yy++)
                for (xx = xh; xx < xh + wh; xx++) {
                    double dx = xx - cx * SCALE, dy = yy - ey;
                    if (inside_roundrect(xx, yy, xh, yh, wh, hh, rh) && (dx * dx * ch * ch + dy * dy * ex * ex <= rhs))
                        px(xx, yy, d->screen_color);
                }
        }
    }
    if (blink > .5 && h >= 8) {
        int sx = (int)round(cx - w * .18 + gaze_x * 3);
        int sy = (int)round(top + h * (.24 + fmax(s->inner, s->outer) * .6) + gaze_y * 2);
        ellipse(sx, sy, 7, 7, d->shine);
    }
}

static void draw_face(const PestinhaDisplay *d, int face, double elapsed)
{
    const double cx = 160, cy = 99;
    EyeShape s = shapes[face < 0 || face > 11 ? 0 : face];
    double blink = 1.0, gx = sin(elapsed * 1.1), gy = sin(elapsed * .7) * .3;
    int side;
    int blinking = fmod(elapsed, 3.7) > .06 && fmod(elapsed, 3.7) < .20;

    rounded_rect(40, 10, 240, 180, 40, d->frame);
    rounded_rect(50, 20, 220, 160, 30, d->shell);
    rounded_rect(62, 32, 196, 136, 24, d->screen_color);

    if (face == 9) {
        for (side = -1; side <= 1; side += 2) {
            int ex = (int)(cx + side * 48), ey = (int)(cy + 2);
            int j;
            for (j = 0; j < 56; j++) {
                double u = (double)j / 55.0;
                int x = ex - 28 + j, y = ey - (int)round(12 * sin(u * 3.141592653589793));
                ellipse(x, y, 5, 5, d->eye);
            }
        }
    } else {
        if (blinking && face != 8) blink = .08;
        for (side = -1; side <= 1; side += 2) {
            double h = s.h;
            if (face == 3 && side == 1) h *= 1.15;
            draw_eye(cx + side * 48 + gx * 8, cy + s.dy * 2 + gy * 6,
                     &(EyeShape){s.w, h, s.inner, s.outer, s.smile, s.dy}, side, gx, gy, blink, d);
        }
    }
    if (face == 8) {
        for (side = -1; side <= 1; side += 2)
            line((int)(cx + side * 48 - 31), (int)(cy + 16), (int)(cx + side * 48 + 31), (int)(cy + 16), d->eye, 7);
    }
    if (d->tie) {
        int tx[] = {153, 167, 166, 154}, ty[] = {142, 142, 154, 154};
        polygon(tx, ty, 4, d->tie);
        {
            int bx[] = {154, 166, 171, 160, 149}, by[] = {153, 153, 176, 184, 176};
            polygon(bx, by, 5, d->tie);
        }
    }
}

static void setup_colors(PestinhaDisplay *d, int jorge)
{
    if (jorge) {
        d->frame = hex(0xe2b13c);
        d->shell = hex(0x14120c);
        d->screen_color = hex(0x1c1a12);
        d->eye = hex(0xffd166);
        d->shine = hex(0xfff3c4);
        d->tie = hex(0xb3261e);
    } else {
        d->frame = hex(0x63e56f);
        d->shell = hex(0x101512);
        d->screen_color = hex(0x171e19);
        d->eye = hex(0x3ee877);
        d->shine = hex(0xc8ffd8);
        d->tie = 0;
    }
}

PestinhaDisplay *display_open(int jorge)
{
    PestinhaDisplay *d = calloc(1, sizeof *d);
    int sn = 0;
    const xcb_setup_t *setup;
    xcb_screen_iterator_t it;
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[2];
    xcb_intern_atom_cookie_t c1, c2;
    xcb_intern_atom_reply_t *a1, *a2;

    if (!d) return NULL;
    d->conn = xcb_connect(NULL, &sn);
    if (!d->conn || xcb_connection_has_error(d->conn)) {
        free(d);
        return NULL;
    }

    setup = xcb_get_setup(d->conn);
    it = xcb_setup_roots_iterator(setup);
    while (sn-- > 0) xcb_screen_next(&it);
    d->screen = it.data;

    d->w = WW;
    d->h = WH;
    d->is_desktop = 0;

    d->win = xcb_generate_id(d->conn);
    values[0] = d->screen->black_pixel;
    values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_create_window(d->conn, d->screen->root_depth, d->win, d->screen->root, 80, 80,
                      (uint16_t)d->w, (uint16_t)d->h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      d->screen->root_visual, mask, values);

    d->gc = xcb_generate_id(d->conn);
    xcb_create_gc(d->conn, d->gc, d->win, 0, NULL);

    c1 = xcb_intern_atom(d->conn, 0, 12, "WM_PROTOCOLS");
    c2 = xcb_intern_atom(d->conn, 0, 16, "WM_DELETE_WINDOW");
    a1 = xcb_intern_atom_reply(d->conn, c1, NULL);
    a2 = xcb_intern_atom_reply(d->conn, c2, NULL);
    if (!a1 || !a2) {
        free(a1); free(a2);
        display_close(d);
        return NULL;
    }
    d->wm_protocols = a1->atom;
    d->wm_delete = a2->atom;
    free(a1); free(a2);

    xcb_change_property(d->conn, XCB_PROP_MODE_REPLACE, d->win, d->wm_protocols,
                        XCB_ATOM_ATOM, 32, 1, &d->wm_delete);

    {
        const char name[] = "Pestinha";
        xcb_change_property(d->conn, XCB_PROP_MODE_REPLACE, d->win,
                            XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, sizeof(name) - 1, name);
    }

    setup_colors(d, jorge);
    xcb_map_window(d->conn, d->win);

    d->pixels = calloc((size_t)W * H, sizeof(uint32_t));
    d->present = calloc((size_t)d->w * d->h, sizeof(uint32_t));
    if (!d->pixels || !d->present) {
        display_close(d);
        return NULL;
    }

    xcb_flush(d->conn);
    canvas = d->pixels;
    return d;
}

PestinhaDisplay *display_open_desktop(int jorge, int level)
{
    PestinhaDisplay *d = calloc(1, sizeof *d);
    int sn = 0;
    const xcb_setup_t *setup;
    xcb_screen_iterator_t it;
    uint32_t mask;
    uint32_t values[3];

    if (!d) return NULL;
    d->conn = xcb_connect(NULL, &sn);
    if (!d->conn || xcb_connection_has_error(d->conn)) {
        free(d);
        return NULL;
    }

    setup = xcb_get_setup(d->conn);
    it = xcb_setup_roots_iterator(setup);
    while (sn-- > 0) xcb_screen_next(&it);
    d->screen = it.data;

    d->is_desktop = 1;
    if (level == 0) {
        d->w = 90; d->h = 68;
    } else if (level == 2) {
        d->w = 160; d->h = 120;
    } else {
        d->w = 120; d->h = 90;
    }

    d->win = xcb_generate_id(d->conn);
    mask = XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    values[0] = d->screen->black_pixel;
    values[1] = 1; /* override redirect para janela flutuante recortada sem moldura */
    values[2] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS |
                XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_STRUCTURE_NOTIFY;

    xcb_create_window(d->conn, d->screen->root_depth, d->win, d->screen->root, 100, 100,
                      (uint16_t)d->w, (uint16_t)d->h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      d->screen->root_visual, mask, values);

    d->gc = xcb_generate_id(d->conn);
    xcb_create_gc(d->conn, d->gc, d->win, 0, NULL);

    /* Formato arredondado recortado via SHAPE */
    xu_shape_roundrect(d->conn, d->win, XU_SHAPE_BOUNDING, d->w, d->h, (int)(d->h * 0.22));
    xu_shape_roundrect(d->conn, d->win, XU_SHAPE_INPUT, d->w, d->h, (int)(d->h * 0.22));

    setup_colors(d, jorge);
    xcb_map_window(d->conn, d->win);

    d->pixels = calloc((size_t)W * H, sizeof(uint32_t));
    d->present = calloc((size_t)d->w * d->h, sizeof(uint32_t));
    if (!d->pixels || !d->present) {
        display_close(d);
        return NULL;
    }

    xcb_flush(d->conn);
    canvas = d->pixels;
    return d;
}

void display_close(PestinhaDisplay *d)
{
    if (!d) return;
    if (d->conn) xcb_disconnect(d->conn);
    free(d->pixels);
    free(d->present);
    free(d);
    canvas = NULL;
}

void display_size(PestinhaDisplay *d, int *w, int *h)
{
    if (!d) return;
    if (w) *w = d->w;
    if (h) *h = d->h;
}

void display_screen(PestinhaDisplay *d, int *sw, int *sh)
{
    if (!d || !d->screen) return;
    if (sw) *sw = (int)d->screen->width_in_pixels;
    if (sh) *sh = (int)d->screen->height_in_pixels;
}

void display_work_area(PestinhaDisplay *d, Area *wa)
{
    if (!d || !d->conn || !d->screen || !wa) return;
    xu_work_area(d->conn, d->screen->root, (int)d->screen->width_in_pixels,
                 (int)d->screen->height_in_pixels, wa);
}

void display_move(PestinhaDisplay *d, int x, int y)
{
    uint32_t vals[2];
    if (!d || !d->conn) return;
    vals[0] = (uint32_t)x;
    vals[1] = (uint32_t)y;
    xcb_configure_window(d->conn, d->win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, vals);
    xcb_flush(d->conn);
}

int display_platforms(PestinhaDisplay *d, int min_top, Platform *out, int max)
{
    if (!d || !d->conn || !d->screen) return 0;
    return xu_list_platforms(d->conn, d->screen->root, (int)d->screen->width_in_pixels,
                             (int)d->screen->height_in_pixels, min_top, out, max);
}

xcb_connection_t *display_connection(PestinhaDisplay *d)
{
    return d ? d->conn : NULL;
}

xcb_window_t display_window(PestinhaDisplay *d)
{
    return d ? d->win : XCB_WINDOW_NONE;
}

int display_poll(PestinhaDisplay *d)
{
    xcb_generic_event_t *e;
    if (!d) return -1;
    while ((e = xcb_poll_for_event(d->conn))) {
        int type = e->response_type & ~0x80, key = 0;
        if (type == XCB_CLIENT_MESSAGE) {
            xcb_client_message_event_t *m = (xcb_client_message_event_t *)e;
            if (m->data.data32[0] == d->wm_delete) {
                free(e);
                return -1;
            }
        } else if (type == XCB_DESTROY_NOTIFY) {
            free(e);
            return -1;
        } else if (type == XCB_KEY_PRESS) {
            xcb_key_press_event_t *k = (xcb_key_press_event_t *)e;
            switch (k->detail) {
            case 54: key = 'c'; break;
            case 33: key = 'p'; break;
            case 25: key = 'w'; break;
            case 53: key = 'x'; break;
            case 46: key = 'l'; break;
            case 55: key = 'v'; break;
            case 41: key = 'f'; break;
            case 58: key = 'm'; break;
            case 38: key = 'a'; break;
            case 28: key = 't'; break;
            case 24: key = 'q'; break;
            default: break;
            }
        }
        free(e);
        if (key) return key;
    }
    return 0;
}

void display_draw(PestinhaDisplay *d, int face, double energy,
                  double cheer, double anger, double sadness, int corner,
                  int sleeping, double elapsed)
{
    int x, y, ox, oy;
    (void)energy; (void)cheer; (void)anger; (void)sadness; (void)corner; (void)sleeping;
    if (!d || !d->conn) return;

    canvas = d->pixels;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            px(x, y, rgb(58, 63, 71));

    draw_face(d, face, elapsed);

    if (d->is_desktop) {
        /* Redimensiona diretamente para d->w x d->h */
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
        /* Modo 100x100 com área 80x60 centralizada */
        for (y = 0; y < WH; y++)
            for (x = 0; x < WW; x++)
                d->present[y * WW + x] = rgb(58, 63, 71);

        for (oy = 0; oy < 60; oy++)
            for (ox = 0; ox < 80; ox++) {
                unsigned int rr = 0, gg = 0, bb = 0, sx, sy;
                for (sy = 0; sy < 9; sy++)
                    for (sx = 0; sx < 9; sx++) {
                        uint32_t p = d->pixels[(30 + oy * 9 + sy) * W + 120 + ox * 9 + sx];
                        rr += (p >> 16) & 255;
                        gg += (p >> 8) & 255;
                        bb += p & 255;
                    }
                d->present[(20 + oy) * WW + 10 + ox] = rgb(rr / 81, gg / 81, bb / 81);
            }
    }

    xcb_put_image(d->conn, XCB_IMAGE_FORMAT_Z_PIXMAP, d->win, d->gc,
                  (uint16_t)d->w, (uint16_t)d->h, 0, 0, 0,
                  d->screen->root_depth, (uint32_t)(d->w * d->h * 4),
                  (const uint8_t *)d->present);
    xcb_flush(d->conn);
}
