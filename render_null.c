/*
 * render_null.c — Backend headless que descarta tudo.
 *
 * Útil para:
 *   • Testes e CI sem display (valgrind, massif, perf).
 *   • Benchmarks do cérebro sem overhead gráfico.
 *   • Troca de backend em tempo de compilação com -DPESTINHA_HEADLESS.
 *
 * GPL-3.0-or-later
 */
#include "render.h"
#include <string.h>

static int null_w, null_h;

static int  null_init(const RenderConfig *cfg)
{
    null_w = cfg->w;
    null_h = cfg->h;
    return 1;
}

static void null_shutdown(void) { null_w = null_h = 0; }

static int  null_poll(RenderEvent *out, int max_ev)
{
    (void)out; (void)max_ev;
    return 0;
}

static void null_present(const uint32_t *pixels, int w, int h,
                         const int dirty[4])
{
    (void)pixels; (void)w; (void)h; (void)dirty;
}

static void null_move(int x, int y)        { (void)x; (void)y; }

static void null_window_size(int *w, int *h) { *w = null_w; *h = null_h; }
static void null_screen_size(int *w, int *h) { *w = null_w; *h = null_h; }

RenderBackend render_backend_null = {
    .name        = "null",
    .init        = null_init,
    .shutdown    = null_shutdown,
    .poll        = null_poll,
    .present     = null_present,
    .move        = null_move,
    .window_size = null_window_size,
    .screen_size = null_screen_size,
};
