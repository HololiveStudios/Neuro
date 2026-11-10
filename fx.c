#define _POSIX_C_SOURCE 200809L
#include "fx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

#include "gfx.h"
#include "xutil.h"

#define MAX_POSTERS 16
#define MAX_PAPERS 16
#define MAX_PARTS 400
#define BALL_SIZE 30
#define PAPER_SIZE 16
#define MAX_RECTS (MAX_POSTERS + MAX_PAPERS + 4 + MAX_PARTS)

typedef struct { int id, x, y, style; Canvas cv; double born, ttl; } Poster;
typedef struct { Body b; double born; int alive; } Paper;
typedef struct { double x, y, vx, vy, life; int color; } Particle;

struct Fx {
    xcb_connection_t *conn;
    xcb_window_t overlay, ballwin;
    xcb_gcontext_t gc, pgc, ballgc;
    int sw, sh, visible;
    uint8_t depth;
    double t, last_full, blink;
    Poster posters[MAX_POSTERS]; int nposters, next_id;
    Paper papers[MAX_PAPERS];
    Particle parts[MAX_PARTS]; int nparts;
    int nb_on, nb_x, nb_y, nb_w, nb_h;
    Canvas nb, ballcv, paper_cv;
    Body ball; int ball_on, ball_held, ball_x, ball_y, ball_mapped;
    xcb_rectangle_t last_rects[MAX_RECTS]; int last_n;
    int dirty;
    FxEvent queue[8]; int qn;
};

static const uint32_t PALETTE[6] = {0xff4d4d, 0xffd24d, 0x58e07a, 0x4dc3ff, 0xc77dff, 0xffffff};

static void paint_ball(Fx *fx)
{
    Canvas *c = &fx->ballcv;
    canvas_fill(c, gfx_rgb(215, 55, 55));
    canvas_disc(c, 15, 15, 14, gfx_rgb(225, 62, 62));
    canvas_rect(c, 2, 13, 26, 4, gfx_rgb(250, 245, 235));
    canvas_disc(c, 10, 9, 3, gfx_rgb(255, 170, 160));
    canvas_border(c, 0, 0, 30, 30, 0, 0);
}

static void paint_paper(Fx *fx)
{
    Canvas *c = &fx->paper_cv;
    canvas_fill(c, gfx_rgb(240, 238, 230));
    canvas_disc(c, 8, 8, 7, gfx_rgb(238, 235, 226));
    canvas_rect(c, 4, 6, 8, 1, gfx_rgb(170, 168, 160));
    canvas_rect(c, 6, 3, 1, 9, gfx_rgb(190, 188, 180));
    canvas_rect(c, 9, 7, 4, 1, gfx_rgb(160, 158, 150));
    canvas_rect(c, 5, 10, 6, 1, gfx_rgb(200, 198, 190));
}

Fx *fx_open(void)
{
    Fx *fx = calloc(1, sizeof *fx);
    int sn = 0;
    xcb_screen_iterator_t it;
    xcb_screen_t *scr;
    uint32_t mask, v[3];
    if (!fx) return NULL;
    fx->conn = xcb_connect(NULL, &sn);
    if (!fx->conn || xcb_connection_has_error(fx->conn)) { free(fx); return NULL; }
    it = xcb_setup_roots_iterator(xcb_get_setup(fx->conn));
    while (sn-- > 0) xcb_screen_next(&it);
    scr = it.data;
    fx->sw = scr->width_in_pixels; fx->sh = scr->height_in_pixels; fx->depth = scr->root_depth;
    fx->next_id = 1; fx->visible = 1;

    fx->overlay = xcb_generate_id(fx->conn);
    mask = XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    v[0] = scr->black_pixel; v[1] = 1; v[2] = XCB_EVENT_MASK_EXPOSURE;
    xcb_create_window(fx->conn, scr->root_depth, fx->overlay, scr->root, 0, 0, (uint16_t)fx->sw, (uint16_t)fx->sh,
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, scr->root_visual, mask, v);
    fx->gc = xcb_generate_id(fx->conn); xcb_create_gc(fx->conn, fx->gc, fx->overlay, 0, NULL);
    fx->pgc = xcb_generate_id(fx->conn); xcb_create_gc(fx->conn, fx->pgc, fx->overlay, 0, NULL);
    xu_shape_set(fx->conn, fx->overlay, XU_SHAPE_INPUT, NULL, 0);      /* o mouse atravessa */
    xu_shape_set(fx->conn, fx->overlay, XU_SHAPE_BOUNDING, NULL, 0);   /* invisivel ate haver objetos */
    {
        const char klass[] = "pestinha-fx\0Pestinha";
        xcb_change_property(fx->conn, XCB_PROP_MODE_REPLACE, fx->overlay, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof klass, klass);
    }
    xcb_map_window(fx->conn, fx->overlay);

    fx->ballwin = xcb_generate_id(fx->conn);
    mask = XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    v[0] = scr->black_pixel; v[1] = 1;
    v[2] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_1_MOTION;
    xcb_create_window(fx->conn, scr->root_depth, fx->ballwin, scr->root, 0, 0, BALL_SIZE, BALL_SIZE, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, scr->root_visual, mask, v);
    fx->ballgc = xcb_generate_id(fx->conn); xcb_create_gc(fx->conn, fx->ballgc, fx->ballwin, 0, NULL);
    xu_shape_ellipse(fx->conn, fx->ballwin, XU_SHAPE_BOUNDING, BALL_SIZE, BALL_SIZE);

    canvas_init(&fx->ballcv, BALL_SIZE, BALL_SIZE); paint_ball(fx);
    canvas_init(&fx->paper_cv, PAPER_SIZE, PAPER_SIZE); paint_paper(fx);
    body_init(&fx->ball, BALL_SIZE, BALL_SIZE, 0.78, 0.7);
    xcb_flush(fx->conn);
    fx->dirty = 1;
    return fx;
}

void fx_close(Fx *fx)
{
    int i;
    if (!fx) return;
    for (i = 0; i < fx->nposters; ++i) canvas_free(&fx->posters[i].cv);
    canvas_free(&fx->nb); canvas_free(&fx->ballcv); canvas_free(&fx->paper_cv);
    if (fx->conn) xcb_disconnect(fx->conn);
    free(fx);
}

void fx_set_visible(Fx *fx, int visible)
{
    if (!fx || fx->visible == visible) return;
    fx->visible = visible;
    if (visible) {
        xcb_map_window(fx->conn, fx->overlay);
        if (fx->ball_on) xcb_map_window(fx->conn, fx->ballwin);
        fx->dirty = 1;
    } else {
        xcb_unmap_window(fx->conn, fx->overlay);
        xcb_unmap_window(fx->conn, fx->ballwin);
    }
    xcb_flush(fx->conn);
}

void fx_raise(Fx *fx)
{
    if (!fx || !fx->visible) return;
    xu_raise(fx->conn, fx->overlay);
    if (fx->ball_on) xu_raise(fx->conn, fx->ballwin);
    xcb_flush(fx->conn);
}

/* ---- cartazes e bilhetes ---- */

static void paint_poster(Canvas *cv, const char *title, const char *sub, int style, int *w, int *h)
{
    char tl[4][64], sl[5][64];
    int tn, sn = 0, i, tw = 0, maxw, pad = 12, boxw = style == FX_STYLE_NOTE ? 150 : 210;
    uint32_t paper, ink, edge, subink;
    switch (style) {
    case FX_STYLE_NOTE:  paper = gfx_rgb(255, 232, 110); ink = gfx_rgb(70, 55, 20); edge = paper; subink = ink; break;
    case FX_STYLE_SIGN:  paper = gfx_rgb(196, 36, 36); ink = gfx_rgb(255, 255, 255); edge = gfx_rgb(255, 255, 255); subink = gfx_rgb(255, 225, 225); break;
    case FX_STYLE_PRANK: paper = gfx_rgb(238, 244, 255); ink = gfx_rgb(35, 80, 190); edge = gfx_rgb(35, 80, 190); subink = gfx_rgb(50, 60, 90); break;
    default:             paper = gfx_rgb(248, 244, 232); ink = gfx_rgb(196, 36, 36); edge = gfx_rgb(196, 36, 36); subink = gfx_rgb(60, 60, 66); break;
    }
    maxw = boxw - 2 * pad;
    tn = style == FX_STYLE_NOTE ? 0 : gfx_wrap(title, maxw, 1, tl, 4);
    if (style == FX_STYLE_NOTE) sn = gfx_wrap(title, maxw, 0, sl, 5);
    else if (sub && *sub) sn = gfx_wrap(sub, maxw, 0, sl, 5);
    for (i = 0; i < tn; ++i) if (gfx_text_width(tl[i], 1) > tw) tw = gfx_text_width(tl[i], 1);
    for (i = 0; i < sn; ++i) if (gfx_text_width(sl[i], 0) > tw) tw = gfx_text_width(sl[i], 0);
    *w = tw + 2 * pad; if (*w < 110) *w = 110;
    *h = pad + tn * 27 + (tn && sn ? 6 : 0) + sn * 17 + pad;
    if (*h < 56) *h = 56;
    canvas_init(cv, *w, *h);
    canvas_fill(cv, paper);
    if (style != FX_STYLE_NOTE) canvas_border(cv, 0, 0, *w, *h, 4, edge);
    else { canvas_rect(cv, *w - 14, *h - 14, 14, 14, gfx_rgb(230, 205, 80)); canvas_rect(cv, 0, 0, *w, 3, gfx_rgb(240, 215, 90)); }
    for (i = 0; i < tn; ++i) canvas_text(cv, (*w - gfx_text_width(tl[i], 1)) / 2, pad + i * 27, tl[i], ink, 1);
    for (i = 0; i < sn; ++i)
        canvas_text(cv, style == FX_STYLE_NOTE ? pad : (*w - gfx_text_width(sl[i], 0)) / 2,
                    pad + tn * 27 + (tn ? 6 : 0) + i * 17, sl[i], style == FX_STYLE_NOTE ? ink : subink, 0);
}

int fx_poster(Fx *fx, int x, int y, const char *title, const char *sub, int style, double ttl)
{
    Poster *p;
    int w, h, i, oldest = 0;
    if (!fx) return 0;
    if (fx->nposters >= MAX_POSTERS) {
        for (i = 1; i < fx->nposters; ++i) if (fx->posters[i].born < fx->posters[oldest].born) oldest = i;
        fx_remove(fx, fx->posters[oldest].id);
    }
    p = &fx->posters[fx->nposters];
    memset(p, 0, sizeof *p);
    paint_poster(&p->cv, title, sub, style, &w, &h);
    if (x > fx->sw - w) x = fx->sw - w;
    if (y > fx->sh - h) y = fx->sh - h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    p->id = fx->next_id++; p->x = x; p->y = y; p->style = style; p->born = fx->t; p->ttl = ttl;
    ++fx->nposters;
    fx->dirty = 1;
    return p->id;
}

void fx_remove(Fx *fx, int id)
{
    int i;
    if (!fx) return;
    for (i = 0; i < fx->nposters; ++i)
        if (fx->posters[i].id == id) {
            canvas_free(&fx->posters[i].cv);
            memmove(&fx->posters[i], &fx->posters[i + 1], (size_t)(fx->nposters - i - 1) * sizeof(Poster));
            --fx->nposters; fx->dirty = 1;
            return;
        }
}

void fx_clear(Fx *fx)
{
    int i;
    if (!fx) return;
    for (i = fx->nposters - 1; i >= 0; --i)
        if (fx->posters[i].style != FX_STYLE_SIGN) fx_remove(fx, fx->posters[i].id);
    for (i = 0; i < MAX_PAPERS; ++i) fx->papers[i].alive = 0;
    fx->dirty = 1;
}

int fx_poster_count(Fx *fx) { return fx ? fx->nposters : 0; }

void fx_paper(Fx *fx, double x, double y, double vx, double vy)
{
    int i, slot = -1;
    if (!fx) return;
    for (i = 0; i < MAX_PAPERS; ++i) if (!fx->papers[i].alive) { slot = i; break; }
    if (slot < 0) {
        slot = 0;
        for (i = 1; i < MAX_PAPERS; ++i) if (fx->papers[i].born < fx->papers[slot].born) slot = i;
    }
    body_init(&fx->papers[slot].b, PAPER_SIZE, PAPER_SIZE, 0.45, 2.5);
    body_place(&fx->papers[slot].b, x, y);
    fx->papers[slot].b.vx = vx; fx->papers[slot].b.vy = vy;
    fx->papers[slot].born = fx->t; fx->papers[slot].alive = 1;
    fx->dirty = 1;
}

int fx_paper_count(Fx *fx)
{
    int i, n = 0;
    if (fx) for (i = 0; i < MAX_PAPERS; ++i) n += fx->papers[i].alive;
    return n;
}

static double frand(void) { return (double)rand() / ((double)RAND_MAX + 1.0); }

void fx_fireworks(Fx *fx, double x, double y, int style)
{
    int i, n = 70;
    if (!fx) return;
    for (i = 0; i < n && fx->nparts < MAX_PARTS; ++i) {
        double a = frand() * 6.283185307, sp = 90.0 + frand() * 260.0;
        Particle *p = &fx->parts[fx->nparts++];
        p->x = x; p->y = y; p->vx = cos(a) * sp; p->vy = sin(a) * sp - 60.0;
        p->life = 1.0 + frand() * 0.9;
        p->color = style == 1 ? (i % 2 ? 0 : 1) : (int)(frand() * 6) % 6;
    }
}

int fx_particle_count(Fx *fx) { return fx ? fx->nparts : 0; }

/* ---- notebook ---- */

static void paint_notebook(Fx *fx, int on_blink)
{
    Canvas *c = &fx->nb;
    int w = fx->nb_w, lid_h = (int)(w * 0.58), base_h = (int)(w * 0.12), i;
    static const uint32_t lines[4] = {0x58e07a, 0x6fa8ff, 0xffc857, 0x58e07a};
    static const int lens[4] = {60, 40, 75, 30};
    if (c->w != w) { canvas_free(c); canvas_init(c, w, lid_h + base_h); }
    canvas_fill(c, gfx_rgb(0, 0, 0));
    canvas_rect(c, 0, 0, w, lid_h, gfx_rgb(58, 60, 70));
    canvas_rect(c, 3, 3, w - 6, lid_h - 6, gfx_rgb(18, 22, 30));
    for (i = 0; i < 4; ++i) {
        int lw = (w - 14) * lens[i] / 100, ly = 8 + i * (lid_h - 20) / 4;
        canvas_rect(c, 7, ly, lw, 2 + w / 60, gfx_rgb((lines[i] >> 16) & 255, (lines[i] >> 8) & 255, lines[i] & 255));
    }
    if (on_blink) canvas_rect(c, 7 + (w - 14) * 40 / 100, lid_h - 14, 5 + w / 40, 4 + w / 50, gfx_rgb(240, 240, 250));
    canvas_rect(c, -0, lid_h, w, base_h, gfx_rgb(176, 180, 190));
    canvas_rect(c, w / 3, lid_h, w / 3, base_h / 2 + 1, gfx_rgb(140, 144, 154));
    canvas_rect(c, 0, lid_h + base_h - 2, w, 2, gfx_rgb(120, 124, 134));
}

void fx_notebook(Fx *fx, int on, int pet_x, int pet_y, int pet_w, int pet_h)
{
    int w, x, y;
    if (!fx) return;
    if (!on) { if (fx->nb_on) { fx->nb_on = 0; fx->dirty = 1; } return; }
    w = pet_w * 65 / 100; if (w < 44) w = 44;
    x = pet_x + (pet_w - w) / 2;
    y = pet_y + pet_h - (int)(w * 0.70 * 0.62);
    if (!fx->nb_on || w != fx->nb_w || x != fx->nb_x || y != fx->nb_y) fx->dirty = 1;
    fx->nb_on = 1; fx->nb_w = w; fx->nb_x = x; fx->nb_y = y;
    fx->nb_h = (int)(w * 0.58) + (int)(w * 0.12);
}

/* ---- bolinha ---- */

void fx_ball_enable(Fx *fx, int on, double x, double y)
{
    if (!fx) return;
    if (on && !fx->ball_on) {
        body_place(&fx->ball, x, y);
        fx->ball_on = 1; fx->ball_x = (int)x; fx->ball_y = (int)y;
        { uint32_t pos[2]; pos[0] = (uint32_t)fx->ball_x; pos[1] = (uint32_t)fx->ball_y;
          xcb_configure_window(fx->conn, fx->ballwin, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, pos); }
        if (fx->visible) { xcb_map_window(fx->conn, fx->ballwin); xu_raise(fx->conn, fx->ballwin); }
        xu_put_canvas(fx->conn, fx->ballwin, fx->ballgc, fx->depth, &fx->ballcv, 0, 0, BALL_SIZE, BALL_SIZE, 0, 0);
    } else if (!on && fx->ball_on) {
        fx->ball_on = 0; fx->ball.dragging = 0; fx->ball_held = 0;
        xcb_unmap_window(fx->conn, fx->ballwin);
    }
    xcb_flush(fx->conn);
}

int fx_ball_enabled(Fx *fx) { return fx && fx->ball_on; }
int fx_ball_held(Fx *fx) { return fx && fx->ball_held; }
void fx_ball_center(Fx *fx, double *cx, double *cy)
{
    if (!fx) return;
    if (cx) *cx = fx->ball.x + BALL_SIZE / 2.0;
    if (cy) *cy = fx->ball.y + BALL_SIZE / 2.0;
}
void fx_ball_kick(Fx *fx, double vx, double vy)
{
    if (!fx || !fx->ball_on || fx->ball.dragging) return;
    fx->ball.vx += vx; fx->ball.vy = vy; fx->ball.grounded = 0; fx->ball.ground_id = 0;
}

/* ---- eventos e quadro ---- */

static void push(Fx *fx, int type, double speed)
{
    if (fx->qn < 8) { fx->queue[fx->qn].type = type; fx->queue[fx->qn].speed = speed; ++fx->qn; }
}

int fx_poll(Fx *fx, FxEvent *out)
{
    xcb_generic_event_t *e;
    if (!fx) return 0;
    while (!fx->qn && (e = xcb_poll_for_event(fx->conn))) {
        int type = e->response_type & ~0x80;
        if (type == XCB_EXPOSE) {
            if (((xcb_expose_event_t *)e)->window == fx->ballwin && fx->ball_on)
                xu_put_canvas(fx->conn, fx->ballwin, fx->ballgc, fx->depth, &fx->ballcv, 0, 0, BALL_SIZE, BALL_SIZE, 0, 0);
            else fx->dirty = 1;
        } else if (type == XCB_BUTTON_PRESS && ((xcb_button_press_event_t *)e)->detail == 1) {
            xcb_button_press_event_t *b = (xcb_button_press_event_t *)e;
            body_grab(&fx->ball, b->root_x, b->root_y, fx->t);
            fx->ball_held = 1; push(fx, FX_BALL_GRAB, 0.0);
        } else if (type == XCB_MOTION_NOTIFY && fx->ball_held) {
            xcb_motion_notify_event_t *m = (xcb_motion_notify_event_t *)e;
            body_drag(&fx->ball, m->root_x, m->root_y, fx->t);
        } else if (type == XCB_BUTTON_RELEASE && fx->ball_held && ((xcb_button_release_event_t *)e)->detail == 1) {
            double speed = 0.0;
            body_release(&fx->ball, fx->t, &speed);
            fx->ball_held = 0; push(fx, FX_BALL_THROW, speed);
        }
        free(e);
    }
    if (fx->qn) {
        *out = fx->queue[0];
        memmove(&fx->queue[0], &fx->queue[1], (size_t)(fx->qn - 1) * sizeof(FxEvent));
        --fx->qn;
        return 1;
    }
    return 0;
}

static void add_rect(xcb_rectangle_t *r, int *n, int x, int y, int w, int h)
{
    if (*n >= MAX_RECTS || w <= 0 || h <= 0) return;
    r[*n].x = (int16_t)x; r[*n].y = (int16_t)y; r[*n].width = (uint16_t)w; r[*n].height = (uint16_t)h; ++*n;
}

void fx_update(Fx *fx, double dt, const Area *work, const Platform *plats, int nplats)
{
    xcb_rectangle_t rects[MAX_RECTS];
    int n = 0, i, blink;
    if (!fx) return;
    fx->t += dt;
    for (i = fx->nposters - 1; i >= 0; --i)
        if (fx->posters[i].ttl > 0.0 && fx->t - fx->posters[i].born > fx->posters[i].ttl) fx_remove(fx, fx->posters[i].id);
    for (i = 0; i < MAX_PAPERS; ++i) {
        if (!fx->papers[i].alive) continue;
        if (fx->t - fx->papers[i].born > 75.0) { fx->papers[i].alive = 0; fx->dirty = 1; continue; }
        body_step(&fx->papers[i].b, dt, work, plats, nplats);
    }
    for (i = fx->nparts - 1; i >= 0; --i) {
        Particle *p = &fx->parts[i];
        p->life -= dt; p->vy += 260.0 * dt; p->x += p->vx * dt; p->y += p->vy * dt;
        p->vx *= exp(-0.6 * dt);
        if (p->life <= 0.0) fx->parts[i] = fx->parts[--fx->nparts];
    }
    if (fx->ball_on) {
        if (fx->ball_held) { /* arrastado: a posicao vem do mouse */ }
        else body_step(&fx->ball, dt, work, plats, nplats);
        if ((int)fx->ball.x != fx->ball_x || (int)fx->ball.y != fx->ball_y) {
            uint32_t pos[2];
            fx->ball_x = (int)fx->ball.x; fx->ball_y = (int)fx->ball.y;
            pos[0] = (uint32_t)fx->ball_x; pos[1] = (uint32_t)fx->ball_y;
            xcb_configure_window(fx->conn, fx->ballwin, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, pos);
        }
    }
    blink = ((int)(fx->t * 2.0)) & 1;
    if (fx->nb_on && blink != (int)fx->blink) fx->dirty = 1;
    fx->blink = blink;

    for (i = 0; i < fx->nposters; ++i) add_rect(rects, &n, fx->posters[i].x, fx->posters[i].y, fx->posters[i].cv.w, fx->posters[i].cv.h);
    for (i = 0; i < MAX_PAPERS; ++i)
        if (fx->papers[i].alive) add_rect(rects, &n, (int)fx->papers[i].b.x, (int)fx->papers[i].b.y, PAPER_SIZE, PAPER_SIZE);
    if (fx->nb_on) {
        int lid_h = (int)(fx->nb_w * 0.58), base_h = (int)(fx->nb_w * 0.12);
        add_rect(rects, &n, fx->nb_x, fx->nb_y, fx->nb_w, lid_h + base_h);
    }
    for (i = 0; i < fx->nparts; ++i) add_rect(rects, &n, (int)fx->parts[i].x - 1, (int)fx->parts[i].y - 1, 4, 4);

    if (fx->dirty || n != fx->last_n || memcmp(rects, fx->last_rects, (size_t)n * sizeof rects[0]) != 0 || fx->t - fx->last_full > 1.5) {
        xu_shape_set(fx->conn, fx->overlay, XU_SHAPE_BOUNDING, rects, n);
        for (i = 0; i < fx->nposters; ++i)
            xu_put_canvas(fx->conn, fx->overlay, fx->gc, fx->depth, &fx->posters[i].cv, 0, 0, fx->posters[i].cv.w, fx->posters[i].cv.h, fx->posters[i].x, fx->posters[i].y);
        for (i = 0; i < MAX_PAPERS; ++i)
            if (fx->papers[i].alive)
                xu_put_canvas(fx->conn, fx->overlay, fx->gc, fx->depth, &fx->paper_cv, 0, 0, PAPER_SIZE, PAPER_SIZE, (int)fx->papers[i].b.x, (int)fx->papers[i].b.y);
        if (fx->nb_on) {
            paint_notebook(fx, blink);
            xu_put_canvas(fx->conn, fx->overlay, fx->gc, fx->depth, &fx->nb, 0, 0, fx->nb.w, fx->nb.h, fx->nb_x, fx->nb_y);
        }
        if (fx->nparts) {
            int c;
            for (c = 0; c < 6; ++c) {
                xcb_rectangle_t pr[MAX_PARTS];
                int pn = 0;
                for (i = 0; i < fx->nparts; ++i)
                    if (fx->parts[i].color == c) {
                        pr[pn].x = (int16_t)(fx->parts[i].x - 1); pr[pn].y = (int16_t)(fx->parts[i].y - 1);
                        pr[pn].width = 3; pr[pn].height = 3; ++pn;
                    }
                if (pn) {
                    uint32_t col = PALETTE[c];
                    xcb_change_gc(fx->conn, fx->pgc, XCB_GC_FOREGROUND, &col);
                    xcb_poly_fill_rectangle(fx->conn, fx->overlay, fx->pgc, (uint32_t)pn, pr);
                }
            }
        }
        memcpy(fx->last_rects, rects, (size_t)n * sizeof rects[0]);
        fx->last_n = n; fx->dirty = 0;
        if (fx->t - fx->last_full > 1.5) fx->last_full = fx->t;
    }
    xcb_flush(fx->conn);
}
