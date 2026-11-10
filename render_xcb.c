/*
 * render_xcb.c — Backend XCB (Linux/X11) para a interface render.h.
 *
 * Estratégia de memória:
 *   • MIT-SHM: o framebuffer é alocado em memória compartilhada entre
 *     o processo e o servidor X, eliminando a cópia interna do xcb_put_image.
 *   • Dirty rect: apenas a região que mudou é copiada para a shm_data
 *     e enviada ao servidor com xcb_copy_area.  Para janelas pequenas
 *     como a Pestinha (≤160×120 = 75 KB) a diferença é pequena, mas o
 *     mecanismo está pronto para escalas maiores.
 *   • Fallback: se MIT-SHM não estiver disponível, usa xcb_put_image
 *     com o subretângulo sujo.
 *
 * Compilar com: -lxcb -lxcb-shm
 * GPL-3.0-or-later
 */
#include "render.h"

#include <xcb/xcb.h>
#include <xcb/shm.h>

#include <sys/shm.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Estado interno (singleton — uma janela por processo)               */
/* ------------------------------------------------------------------ */

typedef struct {
    xcb_connection_t *conn;
    xcb_screen_t     *screen;
    xcb_window_t      win;
    xcb_gcontext_t    gc;

    /* MIT-SHM — campos individuais, sem depender de xcb_image.h */
    int               shm_ok;
    int               shm_shmid;
    uint8_t          *shm_shmaddr;
    xcb_shm_seg_t     shm_seg;      /* xcb_shm_seg_t = uint32_t, em xcb/shm.h */
    xcb_pixmap_t      shm_pixmap;
    uint32_t         *shm_data;    /* ponteiro local para a shm      */

    /* fallback sem SHM */
    uint32_t         *fb_copy;     /* cópia local usada no fallback  */

    int               w, h;
    xcb_atom_t        wm_delete;
} XcbState;

static XcbState X;

/* ------------------------------------------------------------------ */
/* Utilitários                                                         */
/* ------------------------------------------------------------------ */

static xcb_atom_t xcb_intern(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_cookie_t ck =
        xcb_intern_atom(c, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, ck, NULL);
    xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
    free(r);
    return a;
}

/* Clamp inteiro */
static int clamp_i(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ------------------------------------------------------------------ */
/* init                                                                */
/* ------------------------------------------------------------------ */

static int xcb_init(const RenderConfig *cfg)
{
    int snum = 0;
    uint32_t mask, values[2];
    xcb_atom_t wm_protocols;

    memset(&X, 0, sizeof X);

    X.conn = xcb_connect(NULL, &snum);
    if (!X.conn || xcb_connection_has_error(X.conn)) {
        fprintf(stderr, "render_xcb: xcb_connect falhou\n");
        return 0;
    }

    /* Pega o screen */
    {
        xcb_screen_iterator_t it =
            xcb_setup_roots_iterator(xcb_get_setup(X.conn));
        for (int i = 0; i < snum; ++i, xcb_screen_next(&it));
        X.screen = it.data;
    }
    if (!X.screen) {
        xcb_disconnect(X.conn);
        return 0;
    }

    X.w = cfg->w;
    X.h = cfg->h;

    /* Cria a janela */
    X.win = xcb_generate_id(X.conn);
    mask     = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    values[0] = X.screen->black_pixel;
    values[1] = XCB_EVENT_MASK_EXPOSURE
              | XCB_EVENT_MASK_BUTTON_PRESS
              | XCB_EVENT_MASK_BUTTON_RELEASE
              | XCB_EVENT_MASK_POINTER_MOTION
              | XCB_EVENT_MASK_KEY_PRESS
              | XCB_EVENT_MASK_KEY_RELEASE
              | XCB_EVENT_MASK_STRUCTURE_NOTIFY;

    xcb_create_window(X.conn,
        XCB_COPY_FROM_PARENT, X.win, X.screen->root,
        0, 0, (uint16_t)cfg->w, (uint16_t)cfg->h,
        0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
        X.screen->root_visual,
        mask, values);

    /* WM_DELETE_WINDOW */
    wm_protocols = xcb_intern(X.conn, "WM_PROTOCOLS");
    X.wm_delete  = xcb_intern(X.conn, "WM_DELETE_WINDOW");
    xcb_change_property(X.conn, XCB_PROP_MODE_REPLACE,
        X.win, wm_protocols, XCB_ATOM_ATOM, 32, 1, &X.wm_delete);

    if (cfg->title) {
        xcb_change_property(X.conn, XCB_PROP_MODE_REPLACE,
            X.win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
            (uint32_t)strlen(cfg->title), cfg->title);
    }

    /* GC */
    X.gc = xcb_generate_id(X.conn);
    xcb_create_gc(X.conn, X.gc, X.win, 0, NULL);

    /* Tenta MIT-SHM */
    X.shm_ok = 0;
    {
        xcb_shm_query_version_cookie_t ck = xcb_shm_query_version(X.conn);
        xcb_shm_query_version_reply_t *r  =
            xcb_shm_query_version_reply(X.conn, ck, NULL);

        if (r && r->shared_pixmaps) {
            int shmid = shmget(IPC_PRIVATE,
                               (size_t)cfg->w * cfg->h * sizeof(uint32_t),
                               IPC_CREAT | 0600);
            if (shmid >= 0) {
                void *addr = shmat(shmid, NULL, 0);
                if (addr != (void *)-1) {
                    X.shm_shmid   = shmid;
                    X.shm_shmaddr = (uint8_t *)addr;
                    X.shm_seg     = xcb_generate_id(X.conn);
                    xcb_shm_attach(X.conn, X.shm_seg, shmid, 0);
                    /* marca para remoção automática ao fechar */
                    shmctl(shmid, IPC_RMID, NULL);
                    X.shm_data    = (uint32_t *)addr;
                    X.shm_pixmap  = xcb_generate_id(X.conn);
                    xcb_shm_create_pixmap(X.conn, X.shm_pixmap, X.win,
                        (uint16_t)cfg->w, (uint16_t)cfg->h,
                        X.screen->root_depth,
                        X.shm_seg, 0);
                    X.shm_ok = 1;
                }
            }
        }
        free(r);
    }

    /* Fallback: aloca cópia local */
    if (!X.shm_ok) {
        X.fb_copy = calloc((size_t)cfg->w * cfg->h, sizeof(uint32_t));
        if (!X.fb_copy) {
            xcb_disconnect(X.conn);
            return 0;
        }
    }

    xcb_map_window(X.conn, X.win);
    xcb_flush(X.conn);
    return 1;
}

/* ------------------------------------------------------------------ */
/* shutdown                                                            */
/* ------------------------------------------------------------------ */

static void xcb_shutdown(void)
{
    if (!X.conn) return;
    if (X.shm_ok) {
        xcb_shm_detach(X.conn, X.shm_seg);
        shmdt(X.shm_shmaddr);
        xcb_free_pixmap(X.conn, X.shm_pixmap);
    }
    free(X.fb_copy);
    xcb_free_gc(X.conn, X.gc);
    xcb_destroy_window(X.conn, X.win);
    xcb_disconnect(X.conn);
    memset(&X, 0, sizeof X);
}

/* ------------------------------------------------------------------ */
/* poll                                                                */
/* ------------------------------------------------------------------ */

static int xcb_poll(RenderEvent *out, int max_ev)
{
    int count = 0;
    xcb_generic_event_t *ev;

    while (count < max_ev && (ev = xcb_poll_for_event(X.conn))) {
        RenderEvent *re = &out[count];
        re->type   = RENDER_EV_NONE;
        re->x      = 0;
        re->y      = 0;
        re->button = 0;
        re->key    = 0;

        switch (ev->response_type & 0x7f) {

        case XCB_CLIENT_MESSAGE: {
            xcb_client_message_event_t *cm =
                (xcb_client_message_event_t *)ev;
            if (cm->data.data32[0] == X.wm_delete)
                re->type = RENDER_EV_CLOSE;
            break;
        }

        case XCB_EXPOSE:
            re->type = RENDER_EV_EXPOSE;
            break;

        case XCB_MOTION_NOTIFY: {
            xcb_motion_notify_event_t *m =
                (xcb_motion_notify_event_t *)ev;
            re->type = RENDER_EV_MOUSE_MOVE;
            re->x    = m->event_x;
            re->y    = m->event_y;
            break;
        }

        case XCB_BUTTON_PRESS: {
            xcb_button_press_event_t *b =
                (xcb_button_press_event_t *)ev;
            re->type   = RENDER_EV_MOUSE_DOWN;
            re->x      = b->event_x;
            re->y      = b->event_y;
            re->button = b->detail;
            break;
        }

        case XCB_BUTTON_RELEASE: {
            xcb_button_release_event_t *b =
                (xcb_button_release_event_t *)ev;
            re->type   = RENDER_EV_MOUSE_UP;
            re->x      = b->event_x;
            re->y      = b->event_y;
            re->button = b->detail;
            break;
        }

        case XCB_KEY_PRESS: {
            xcb_key_press_event_t *k = (xcb_key_press_event_t *)ev;
            re->type = RENDER_EV_KEY_DOWN;
            re->key  = k->detail;
            break;
        }

        case XCB_KEY_RELEASE: {
            xcb_key_release_event_t *k = (xcb_key_release_event_t *)ev;
            re->type = RENDER_EV_KEY_UP;
            re->key  = k->detail;
            break;
        }

        default:
            break;
        }

        free(ev);
        if (re->type != RENDER_EV_NONE)
            ++count;
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* present                                                             */
/* ------------------------------------------------------------------ */

static void xcb_present(const uint32_t *pixels, int w, int h,
                        const int dirty[4])
{
    int dx, dy, dw, dh;

    /* Calcula o retângulo a enviar */
    if (dirty) {
        dx = clamp_i(dirty[0], 0, w);
        dy = clamp_i(dirty[1], 0, h);
        dw = clamp_i(dirty[2], 0, w - dx);
        dh = clamp_i(dirty[3], 0, h - dy);
    } else {
        dx = 0; dy = 0; dw = w; dh = h;
    }
    if (dw <= 0 || dh <= 0) return;

    if (X.shm_ok) {
        /*
         * Copia somente as linhas do dirty rect para a shm.
         * Para janelas pequenas, copiar o frame inteiro seria igualmente
         * rápido, mas manter o dirty rect aqui torna o padrão explícito.
         */
        int row;
        size_t row_bytes = (size_t)dw * sizeof(uint32_t);
        for (row = dy; row < dy + dh; ++row) {
            memcpy(X.shm_data  + row * w + dx,
                   pixels       + row * w + dx,
                   row_bytes);
        }
        xcb_copy_area(X.conn, X.shm_pixmap, X.win, X.gc,
                      (int16_t)dx, (int16_t)dy,
                      (int16_t)dx, (int16_t)dy,
                      (uint16_t)dw, (uint16_t)dh);
    } else {
        /*
         * Fallback: xcb_put_image para o dirty rect.
         * Monta um buffer temporário com apenas as linhas necessárias.
         */
        uint32_t *sub = X.fb_copy; /* reutiliza o buffer pré-alocado */
        int row;
        size_t row_bytes = (size_t)dw * sizeof(uint32_t);

        for (row = 0; row < dh; ++row)
            memcpy(sub + (size_t)row * dw,
                   pixels + (size_t)(dy + row) * w + dx,
                   row_bytes);

        xcb_put_image(X.conn,
                      XCB_IMAGE_FORMAT_Z_PIXMAP,
                      X.win, X.gc,
                      (uint16_t)dw, (uint16_t)dh,
                      (int16_t)dx, (int16_t)dy,
                      0,
                      X.screen->root_depth,
                      (uint32_t)(dw * dh * sizeof(uint32_t)),
                      (const uint8_t *)sub);
    }

    xcb_flush(X.conn);
}

/* ------------------------------------------------------------------ */
/* move / sizes                                                        */
/* ------------------------------------------------------------------ */

static void xcb_move(int x, int y)
{
    uint32_t v[2] = { (uint32_t)x, (uint32_t)y };
    xcb_configure_window(X.conn, X.win,
        XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, v);
    xcb_flush(X.conn);
}

static void xcb_window_size(int *w, int *h)
{
    *w = X.w;
    *h = X.h;
}

static void xcb_screen_size(int *w, int *h)
{
    if (X.screen) {
        *w = X.screen->width_in_pixels;
        *h = X.screen->height_in_pixels;
    } else {
        *w = *h = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Backend exportado                                                   */
/* ------------------------------------------------------------------ */

RenderBackend render_backend_xcb = {
    .name        = "xcb",
    .init        = xcb_init,
    .shutdown    = xcb_shutdown,
    .poll        = xcb_poll,
    .present     = xcb_present,
    .move        = xcb_move,
    .window_size = xcb_window_size,
    .screen_size = xcb_screen_size,
};
