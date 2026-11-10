#include "xutil.h"

#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <xcb/xcbext.h>

static xcb_extension_t shape_ext = { "SHAPE", 0 };

typedef struct {
    uint8_t major_opcode, minor_opcode;
    uint16_t length;
    uint8_t operation, destination_kind, ordering, pad0;
    uint32_t destination_window;
    int16_t x_offset, y_offset;
} ShapeRectanglesRequest;

int xu_has_shape(xcb_connection_t *c)
{
    const xcb_query_extension_reply_t *ext = xcb_get_extension_data(c, &shape_ext);
    return ext && ext->present;
}

void xu_shape_set(xcb_connection_t *c, xcb_window_t win, int kind,
                  const xcb_rectangle_t *rects, int n)
{
    static const xcb_protocol_request_t request = { 4, &shape_ext, 1 /* Rectangles */, 1 };
    struct iovec parts[6];
    ShapeRectanglesRequest out;
    if (!xu_has_shape(c)) return;
    memset(&out, 0, sizeof out);
    out.operation = 0;            /* Set */
    out.destination_kind = (uint8_t)kind;
    out.ordering = 0;             /* Unsorted */
    out.destination_window = win;
    parts[2].iov_base = &out;               parts[2].iov_len = sizeof out;
    parts[3].iov_base = NULL;               parts[3].iov_len = 0;
    parts[4].iov_base = (void *)rects;      parts[4].iov_len = (size_t)n * sizeof(xcb_rectangle_t);
    parts[5].iov_base = NULL;               parts[5].iov_len = 0;
    xcb_send_request(c, 0, parts + 2, &request);
}

void xu_shape_roundrect(xcb_connection_t *c, xcb_window_t win, int kind, int w, int h, int r)
{
    xcb_rectangle_t rows[256];
    int n = 0, y;
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    for (y = 0; y < h && n < 255; ++y) {
        int inset = 0;
        if (y < r) { int dy = r - y - 1; double dx = r - __builtin_sqrt((double)(r * r - dy * dy)); inset = (int)(dx + 0.5); }
        else if (y >= h - r) { int dy = y - (h - r); double dx = r - __builtin_sqrt((double)(r * r - dy * dy)); inset = (int)(dx + 0.5); }
        rows[n].x = (int16_t)inset; rows[n].y = (int16_t)y;
        rows[n].width = (uint16_t)(w - 2 * inset); rows[n].height = 1; ++n;
    }
    xu_shape_set(c, win, kind, rows, n);
}

void xu_shape_ellipse(xcb_connection_t *c, xcb_window_t win, int kind, int w, int h)
{
    xcb_rectangle_t rows[256];
    int n = 0, y;
    for (y = 0; y < h && n < 255; ++y) {
        double ny = ((double)y + 0.5 - h / 2.0) / (h / 2.0);
        double half = (w / 2.0) * __builtin_sqrt(1.0 - ny * ny);
        int x0 = (int)(w / 2.0 - half + 0.5), x1 = (int)(w / 2.0 + half + 0.5);
        if (x1 <= x0) continue;
        rows[n].x = (int16_t)x0; rows[n].y = (int16_t)y;
        rows[n].width = (uint16_t)(x1 - x0); rows[n].height = 1; ++n;
    }
    xu_shape_set(c, win, kind, rows, n);
}

xcb_atom_t xu_atom(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, (uint16_t)strlen(name), name), NULL);
    xcb_atom_t atom = r ? r->atom : XCB_ATOM_NONE;
    free(r);
    return atom;
}

void xu_put_canvas(xcb_connection_t *c, xcb_drawable_t win, xcb_gcontext_t gc, uint8_t depth,
                   const Canvas *canvas, int sx, int sy, int w, int h, int dx, int dy)
{
    uint32_t maxbytes = xcb_get_maximum_request_length(c) * 4;
    int rows_per, row = 0, i;
    uint32_t *tmp;
    if (sx < 0 || sy < 0 || w <= 0 || h <= 0 || sx + w > canvas->w || sy + h > canvas->h) return;
    maxbytes = maxbytes > 64 ? maxbytes - 64 : maxbytes;
    rows_per = (int)(maxbytes / ((uint32_t)w * 4u));
    if (rows_per < 1) rows_per = 1;
    if (rows_per > h) rows_per = h;
    tmp = malloc((size_t)w * (size_t)rows_per * 4u);
    if (!tmp) return;
    while (row < h) {
        int n = h - row < rows_per ? h - row : rows_per;
        for (i = 0; i < n; ++i)
            memcpy(tmp + (size_t)i * w, canvas->px + (size_t)(sy + row + i) * canvas->w + sx, (size_t)w * 4u);
        xcb_put_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, win, gc, (uint16_t)w, (uint16_t)n,
                      (int16_t)dx, (int16_t)(dy + row), 0, depth, (uint32_t)w * (uint32_t)n * 4u,
                      (const uint8_t *)tmp);
        row += n;
    }
    free(tmp);
}

void xu_raise(xcb_connection_t *c, xcb_window_t win)
{
    uint32_t above = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(c, win, XCB_CONFIG_WINDOW_STACK_MODE, &above);
}

void xu_work_area(xcb_connection_t *c, xcb_window_t root, int sw, int sh, Area *out)
{
    xcb_atom_t atom = xu_atom(c, "_NET_WORKAREA");
    out->x = 0; out->y = 0; out->w = sw; out->h = sh;
    if (atom == XCB_ATOM_NONE) return;
    {
        xcb_get_property_reply_t *r = xcb_get_property_reply(
            c, xcb_get_property(c, 0, root, atom, XCB_ATOM_CARDINAL, 0, 4), NULL);
        if (r && r->format == 32 && xcb_get_property_value_length(r) >= 16) {
            const uint32_t *v = xcb_get_property_value(r);
            if (v[2] >= 320 && v[3] >= 240 && (int)v[0] + (int)v[2] <= sw + 1 && (int)v[1] + (int)v[3] <= sh + 1) {
                out->x = (int)v[0]; out->y = (int)v[1]; out->w = (int)v[2]; out->h = (int)v[3];
            }
        }
        free(r);
    }
}

int xu_list_platforms(xcb_connection_t *c, xcb_window_t root, int sw, int sh,
                      int min_top, Platform *out, int max)
{
    xcb_query_tree_reply_t *tree = xcb_query_tree_reply(c, xcb_query_tree(c, root), NULL);
    xcb_window_t *kids;
    int n, i, j, k = 0;
    xcb_get_window_attributes_cookie_t attr[192];
    xcb_get_geometry_cookie_t geo[192];
    int vis[192], rx[192], ry[192], rw[192], rh[192];
    if (!tree) return 0;
    n = xcb_query_tree_children_length(tree);
    kids = xcb_query_tree_children(tree);
    if (n > 192) { kids += n - 192; n = 192; }   /* os 192 de cima da pilha */
    for (i = 0; i < n; ++i) {
        attr[i] = xcb_get_window_attributes(c, kids[i]);
        geo[i] = xcb_get_geometry(c, kids[i]);
    }
    for (i = 0; i < n; ++i) {
        xcb_get_window_attributes_reply_t *a = xcb_get_window_attributes_reply(c, attr[i], NULL);
        xcb_get_geometry_reply_t *g = xcb_get_geometry_reply(c, geo[i], NULL);
        vis[i] = 0;
        if (a && g && a->map_state == XCB_MAP_STATE_VIEWABLE && !a->override_redirect) {
            vis[i] = 1;
            rx[i] = g->x; ry[i] = g->y;
            rw[i] = g->width + 2 * g->border_width; rh[i] = g->height + 2 * g->border_width;
        }
        free(a); free(g);
    }
    for (i = 0; i < n && k < max; ++i) {
        int covered = 0, mx, my;
        if (!vis[i]) continue;
        if (rw[i] < 160 || rh[i] < 64) continue;
        if (ry[i] < min_top || ry[i] > sh - 48) continue;
        if (rw[i] >= sw * 97 / 100 && rh[i] >= sh * 90 / 100) continue;
        mx = rx[i] + rw[i] / 2; my = ry[i] + 2;
        for (j = i + 1; j < n; ++j)   /* janela acima na pilha cobrindo o meio do topo? */
            if (vis[j] && mx >= rx[j] && mx < rx[j] + rw[j] && my >= ry[j] && my < ry[j] + rh[j]) { covered = 1; break; }
        if (covered) continue;
        out[k].id = kids[i]; out[k].x = rx[i]; out[k].y = ry[i]; out[k].w = rw[i]; out[k].h = rh[i]; ++k;
    }
    free(tree);
    return k;
}
