#define _POSIX_C_SOURCE 200809L
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

#include "gfx.h"
#include "xutil.h"

#define MAX_ITEMS 24
#define ITEM_H 24
#define SEP_H 9

typedef struct { char label[72]; int action, check, sep, y, h; } Item;
typedef struct { char label[56]; int action, x, y, w, h; } Button;

struct Ui {
    xcb_connection_t *conn;
    xcb_screen_t *scr;
    xcb_window_t root, menu, dlg;
    xcb_gcontext_t menu_gc, dlg_gc;
    uint8_t depth;
    UiState st;
    int sw, sh;
    Item items[MAX_ITEMS]; int nitems, hover, mx, my, mw, mh;
    Canvas mc, dc;
    Button buttons[4]; int nbuttons, dlg_first, forget_armed, dlg_hover, dw, dh, dx, dy;
    xcb_keycode_t key_p, key_esc;
    int hotkey_ok;
};

static const uint32_t BG = 0x1e2028, EDGE = 0x464a5c, FG = 0xe6e8f0, HOT = 0x3a6ee6, DIM = 0x9aa0b4;

static xcb_keycode_t keycode_for(Ui *u, xcb_keysym_t sym)
{
    const xcb_setup_t *s = xcb_get_setup(u->conn);
    int first = s->min_keycode, count = s->max_keycode - s->min_keycode + 1, i, j;
    xcb_keycode_t found = 0;
    xcb_get_keyboard_mapping_reply_t *r = xcb_get_keyboard_mapping_reply(
        u->conn, xcb_get_keyboard_mapping(u->conn, (xcb_keycode_t)first, (uint8_t)count), NULL);
    if (!r) return 0;
    {
        int per = r->keysyms_per_keycode;
        const xcb_keysym_t *ks = xcb_get_keyboard_mapping_keysyms(r);
        for (i = 0; i < count && !found; ++i)
            for (j = 0; j < per; ++j)
                if (ks[i * per + j] == sym) { found = (xcb_keycode_t)(first + i); break; }
    }
    free(r);
    return found;
}

Ui *ui_open(void)
{
    Ui *u = calloc(1, sizeof *u);
    int sn = 0;
    xcb_screen_iterator_t it;
    if (!u) return NULL;
    u->conn = xcb_connect(NULL, &sn);
    if (!u->conn || xcb_connection_has_error(u->conn)) { free(u); return NULL; }
    it = xcb_setup_roots_iterator(xcb_get_setup(u->conn));
    while (sn-- > 0) xcb_screen_next(&it);
    u->scr = it.data; u->root = u->scr->root; u->depth = u->scr->root_depth;
    u->sw = u->scr->width_in_pixels; u->sh = u->scr->height_in_pixels;
    u->st.sensors = 1; u->st.platforms = 1; u->st.size = 1;
    u->key_p = keycode_for(u, 0x70);
    u->key_esc = keycode_for(u, 0xff1b);
    return u;
}

static void menu_destroy(Ui *u)
{
    if (!u->menu) return;
    xcb_ungrab_pointer(u->conn, XCB_CURRENT_TIME);
    xcb_ungrab_keyboard(u->conn, XCB_CURRENT_TIME);
    xcb_destroy_window(u->conn, u->menu);
    canvas_free(&u->mc);
    u->menu = 0;
    xcb_flush(u->conn);
}

static void dlg_destroy(Ui *u)
{
    if (!u->dlg) return;
    xcb_destroy_window(u->conn, u->dlg);
    canvas_free(&u->dc);
    u->dlg = 0; u->forget_armed = 0;
    xcb_flush(u->conn);
}

void ui_close(Ui *u)
{
    if (!u) return;
    menu_destroy(u); dlg_destroy(u);
    if (u->conn) xcb_disconnect(u->conn);
    free(u);
}

int ui_hotkey(Ui *u)
{
    static const uint16_t extra[4] = {0, XCB_MOD_MASK_LOCK, XCB_MOD_MASK_2, XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2};
    int i, ok = 1;
    if (!u || !u->key_p) return 0;
    for (i = 0; i < 4; ++i) {
        xcb_void_cookie_t ck = xcb_grab_key_checked(u->conn, 1, u->root,
            (uint16_t)(XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1 | extra[i]), u->key_p,
            XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        xcb_generic_error_t *err = xcb_request_check(u->conn, ck);
        if (err) { ok = 0; free(err); }
    }
    u->hotkey_ok = ok;
    return ok;
}

void ui_raise(Ui *u)
{
    if (!u) return;
    if (u->menu) xu_raise(u->conn, u->menu);
    if (u->dlg) xu_raise(u->conn, u->dlg);
    xcb_flush(u->conn);
}

int ui_busy(Ui *u) { return u && (u->menu || u->dlg); }

/* ---- menu ---- */

static void add_item(Ui *u, const char *label, int action, int check, int sep)
{
    Item *it;
    if (u->nitems >= MAX_ITEMS) return;
    it = &u->items[u->nitems++];
    memset(it, 0, sizeof *it);
    if (label) snprintf(it->label, sizeof it->label, "%s", label);
    it->action = action; it->check = check; it->sep = sep;
}

static void build_menu(Ui *u)
{
    static const char *const SIZES[3] = {"pequeno", "médio", "grande"};
    char sz[48];
    int i, y = 4, maxw = 0;
    u->nitems = 0;
    add_item(u, "Carinho", UI_CARESS, -1, 0);
    add_item(u, "Cutucar", UI_POKE, -1, 0);
    add_item(u, "Dar trabalho", UI_WORK, -1, 0);
    add_item(u, "Dispensar", UI_DISMISS, -1, 0);
    add_item(u, NULL, 0, -1, 1);
    add_item(u, "Mandar para o castigo", UI_CORNER, -1, 0);
    add_item(u, "Visitar no castigo", UI_VISIT, -1, 0);
    add_item(u, "Soltar do castigo", UI_RELEASE, -1, 0);
    add_item(u, NULL, 0, -1, 1);
    add_item(u, "Travessura", UI_MISCHIEF, -1, 0);
    add_item(u, "Arremessar", UI_THROW, -1, 0);
    add_item(u, NULL, 0, -1, 1);
    add_item(u, "Bolinha", UI_BALL, u->st.ball ? 1 : 0, 0);
    add_item(u, "Pousar nas janelas", UI_PLATFORMS, u->st.platforms ? 1 : 0, 0);
    add_item(u, "Sensores do PC", UI_SENSORS, u->st.sensors ? 1 : 0, 0);
    snprintf(sz, sizeof sz, "Tamanho: %s", SIZES[u->st.size < 0 ? 0 : u->st.size > 2 ? 2 : u->st.size]);
    add_item(u, sz, UI_SIZE, -1, 0);
    add_item(u, "Limpar cartazes e papéis", UI_CLEAR_FX, -1, 0);
    add_item(u, NULL, 0, -1, 1);
    add_item(u, "Privacidade e memória...", UI_NONE + 1000, -1, 0);   /* tratada aqui dentro */
    add_item(u, u->hotkey_ok ? "Esconder (Ctrl+Alt+P)" : "Esconder", UI_HIDE, -1, 0);
    add_item(u, "Sair", UI_QUIT, -1, 0);
    for (i = 0; i < u->nitems; ++i) {
        Item *it = &u->items[i];
        it->y = y; it->h = it->sep ? SEP_H : ITEM_H; y += it->h;
        if (!it->sep && gfx_text_width(it->label, 0) > maxw) maxw = gfx_text_width(it->label, 0);
    }
    u->mw = maxw + 20 + 24; u->mh = y + 4;
}

static void draw_menu(Ui *u)
{
    int i;
    canvas_fill(&u->mc, BG);
    canvas_border(&u->mc, 0, 0, u->mw, u->mh, 1, EDGE);
    for (i = 0; i < u->nitems; ++i) {
        Item *it = &u->items[i];
        if (it->sep) { canvas_rect(&u->mc, 8, it->y + SEP_H / 2, u->mw - 16, 1, 0x3c3f50); continue; }
        if (i == u->hover) canvas_rect(&u->mc, 3, it->y, u->mw - 6, it->h, HOT);
        if (it->check >= 0) {
            canvas_border(&u->mc, 9, it->y + 6, 12, 12, 1, i == u->hover ? 0xffffff : DIM);
            if (it->check) canvas_rect(&u->mc, 12, it->y + 9, 6, 6, 0xffffff);
        }
        canvas_text(&u->mc, 28, it->y + (it->h - 17) / 2, it->label, i == u->hover ? 0xffffff : FG, 0);
    }
    xu_put_canvas(u->conn, u->menu, u->menu_gc, u->depth, &u->mc, 0, 0, u->mw, u->mh, 0, 0);
    xcb_flush(u->conn);
}

void ui_menu_open(Ui *u, int x, int y)
{
    uint32_t mask, v[3];
    xcb_grab_pointer_reply_t *g;
    if (!u) return;
    menu_destroy(u);
    build_menu(u);
    if (x + u->mw > u->sw) x = u->sw - u->mw;
    if (y + u->mh > u->sh) y = u->sh - u->mh;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    u->mx = x; u->my = y; u->hover = -1;
    canvas_init(&u->mc, u->mw, u->mh);
    u->menu = xcb_generate_id(u->conn);
    mask = XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    v[0] = u->scr->black_pixel; v[1] = 1;
    v[2] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
           XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_KEY_PRESS;
    xcb_create_window(u->conn, u->depth, u->menu, u->root, (int16_t)x, (int16_t)y, (uint16_t)u->mw, (uint16_t)u->mh, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, u->scr->root_visual, mask, v);
    u->menu_gc = xcb_generate_id(u->conn); xcb_create_gc(u->conn, u->menu_gc, u->menu, 0, NULL);
    xcb_map_window(u->conn, u->menu);
    xu_raise(u->conn, u->menu);
    g = xcb_grab_pointer_reply(u->conn, xcb_grab_pointer(u->conn, 1, u->menu,
        XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION,
        XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME), NULL);
    free(g);
    free(xcb_grab_keyboard_reply(u->conn, xcb_grab_keyboard(u->conn, 1, u->menu, XCB_CURRENT_TIME,
        XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC), NULL));
    draw_menu(u);
}

static int menu_hit(Ui *u, int rx, int ry)
{
    int i, lx = rx - u->mx, ly = ry - u->my;
    if (lx < 0 || ly < 0 || lx >= u->mw || ly >= u->mh) return -2;   /* fora */
    for (i = 0; i < u->nitems; ++i)
        if (!u->items[i].sep && ly >= u->items[i].y && ly < u->items[i].y + u->items[i].h) return i;
    return -1;
}

/* ---- janela de privacidade ---- */

static void layout_dialog(Ui *u, int first)
{
    u->nbuttons = 0;
    if (first) {
        snprintf(u->buttons[0].label, sizeof u->buttons[0].label, "Ligar sensores");
        u->buttons[0].action = UI_SENSORS_ON;
        snprintf(u->buttons[1].label, sizeof u->buttons[1].label, "Agora não");
        u->buttons[1].action = UI_SENSORS_OFF;
        u->nbuttons = 2;
    } else {
        snprintf(u->buttons[0].label, sizeof u->buttons[0].label, "Sensores: %s", u->st.sensors ? "LIGADOS" : "DESLIGADOS");
        u->buttons[0].action = UI_SENSORS;
        snprintf(u->buttons[1].label, sizeof u->buttons[1].label, "%s", u->forget_armed ? "Confirmar: apagar tudo?" : "Apagar a memória");
        u->buttons[1].action = UI_FORGET;
        snprintf(u->buttons[2].label, sizeof u->buttons[2].label, "Fechar");
        u->buttons[2].action = UI_NONE;
        u->nbuttons = 3;
    }
}

static void draw_dialog(Ui *u)
{
    static const char *const FIRST[] = {
        "O Pestinha é um bichinho virtual que vive na sua tela, aprende com carinho e reage ao que você faz no computador.",
        "Tudo roda só neste computador: nada é enviado pela rede.",
        NULL};
    static const char *const BODY[] = {
        "O Pestinha roda só neste computador e não envia nada pela rede.",
        "O que os sensores leem (tudo só na memória, nada é gravado):",
        NULL};
    static const char *const SENS[] = {
        "- posição e velocidade do mouse;",
        "- ritmo de digitação: só conta quantas teclas foram apertadas, nenhuma tecla é registrada;",
        "- a classe do aplicativo ativo (nunca o título da janela);",
        "- se há áudio tocando (/proc/asound);",
        "- posição e tamanho das janelas, para ele pousar nelas.",
        NULL};
    int i, y = 14, n, k, bx = 16;
    char lines[8][64];
    const char *const *p;
    canvas_fill(&u->dc, BG);
    canvas_border(&u->dc, 0, 0, u->dw, u->dh, 1, EDGE);
    canvas_text(&u->dc, 16, y, u->dlg_first ? "Bem-vindo ao Pestinha!" : "Privacidade e memória", 0xffffff, 1);
    y += 40;
    for (p = u->dlg_first ? FIRST : BODY; *p; ++p) {
        n = gfx_wrap(*p, u->dw - 32, 0, lines, 8);
        for (k = 0; k < n; ++k) { canvas_text(&u->dc, 16, y, lines[k], FG, 0); y += 17; }
        y += 4;
    }
    if (u->dlg_first) {
        n = gfx_wrap("Os sensores deixam ele reagir ao seu uso (mouse, digitação, aplicativo em foco, áudio). Você pode ligar ou desligar quando quiser pelo menu.", u->dw - 32, 0, lines, 8);
        for (k = 0; k < n; ++k) { canvas_text(&u->dc, 16, y, lines[k], FG, 0); y += 17; }
        y += 4;
        n = gfx_wrap("Só contam o ritmo das teclas (nenhuma é registrada) e a classe do app ativo, nunca o título.", u->dw - 32, 0, lines, 8);
        for (k = 0; k < n; ++k) { canvas_text(&u->dc, 16, y, lines[k], DIM, 0); y += 17; }
    } else {
        for (p = SENS; *p; ++p) {
            n = gfx_wrap(*p, u->dw - 40, 0, lines, 8);
            for (k = 0; k < n; ++k) { canvas_text(&u->dc, 24, y, lines[k], DIM, 0); y += 17; }
        }
        y += 6;
        n = gfx_wrap(u->hotkey_ok ? "Atalho de pânico: Ctrl+Alt+P esconde o Pestinha e desliga os sensores." : "O atalho de pânico não pôde ser registrado (outro programa usa Ctrl+Alt+P).", u->dw - 32, 0, lines, 8);
        for (k = 0; k < n; ++k) { canvas_text(&u->dc, 16, y, lines[k], FG, 0); y += 17; }
    }
    layout_dialog(u, u->dlg_first);
    for (i = 0; i < u->nbuttons; ++i) {
        Button *b = &u->buttons[i];
        b->w = gfx_text_width(b->label, 0) + 28; b->h = 30; b->x = bx; b->y = u->dh - 46;
        bx += b->w + 10;
        canvas_rect(&u->dc, b->x, b->y, b->w, b->h, i == u->dlg_hover ? HOT : 0x2c3040);
        canvas_border(&u->dc, b->x, b->y, b->w, b->h, 1, EDGE);
        canvas_text(&u->dc, b->x + 14, b->y + 6, b->label, 0xffffff, 0);
    }
    xu_put_canvas(u->conn, u->dlg, u->dlg_gc, u->depth, &u->dc, 0, 0, u->dw, u->dh, 0, 0);
    xcb_flush(u->conn);
}

void ui_privacy_open(Ui *u, int first_run)
{
    uint32_t mask, v[3];
    if (!u) return;
    dlg_destroy(u);
    u->dlg_first = first_run; u->forget_armed = 0; u->dlg_hover = -1;
    u->dw = 520; u->dh = first_run ? 290 : 420;
    u->dx = (u->sw - u->dw) / 2; u->dy = (u->sh - u->dh) / 3;
    canvas_init(&u->dc, u->dw, u->dh);
    u->dlg = xcb_generate_id(u->conn);
    mask = XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    v[0] = u->scr->black_pixel; v[1] = 1;
    v[2] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_LEAVE_WINDOW;
    xcb_create_window(u->conn, u->depth, u->dlg, u->root, (int16_t)u->dx, (int16_t)u->dy, (uint16_t)u->dw, (uint16_t)u->dh, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, u->scr->root_visual, mask, v);
    u->dlg_gc = xcb_generate_id(u->conn); xcb_create_gc(u->conn, u->dlg_gc, u->dlg, 0, NULL);
    xcb_map_window(u->conn, u->dlg);
    xu_raise(u->conn, u->dlg);
    draw_dialog(u);
}

void ui_set_state(Ui *u, const UiState *st)
{
    if (!u) return;
    u->st = *st;
    if (u->dlg && !u->dlg_first) draw_dialog(u);
}

static int dialog_hit(Ui *u, int x, int y)
{
    int i;
    for (i = 0; i < u->nbuttons; ++i)
        if (x >= u->buttons[i].x && x < u->buttons[i].x + u->buttons[i].w && y >= u->buttons[i].y && y < u->buttons[i].y + u->buttons[i].h) return i;
    return -1;
}

int ui_poll(Ui *u, UiAction *action)
{
    xcb_generic_event_t *e;
    if (!u) return 0;
    while ((e = xcb_poll_for_event(u->conn))) {
        int type = e->response_type & ~0x80, result = 0;
        UiAction act = UI_NONE;
        if (type == XCB_MOTION_NOTIFY) {
            xcb_motion_notify_event_t *m = (xcb_motion_notify_event_t *)e;
            if (u->menu) {
                int h = menu_hit(u, m->root_x, m->root_y);
                if (h < 0) h = -1;
                if (h != u->hover) { u->hover = h; draw_menu(u); }
            } else if (u->dlg && m->event == u->dlg) {
                int h = dialog_hit(u, m->event_x, m->event_y);
                if (h != u->dlg_hover) { u->dlg_hover = h; draw_dialog(u); }
            }
        } else if (type == XCB_BUTTON_PRESS) {
            xcb_button_press_event_t *b = (xcb_button_press_event_t *)e;
            if (u->menu) {
                int h = menu_hit(u, b->root_x, b->root_y);
                if (h >= 0) {
                    int a = u->items[h].action;
                    menu_destroy(u);
                    if (a == UI_NONE + 1000) ui_privacy_open(u, 0);
                    else { act = (UiAction)a; result = 1; }
                } else menu_destroy(u);
            } else if (u->dlg && b->event == u->dlg) {
                int h = dialog_hit(u, b->event_x, b->event_y);
                if (h >= 0) {
                    int a = u->buttons[h].action;
                    if (a == UI_FORGET && !u->forget_armed) { u->forget_armed = 1; draw_dialog(u); }
                    else if (a == UI_FORGET) { dlg_destroy(u); act = UI_FORGET; result = 1; }
                    else if (a == UI_SENSORS) { act = UI_SENSORS; result = 1; }
                    else { dlg_destroy(u); if (a != UI_NONE) { act = (UiAction)a; result = 1; } }
                } else if (u->forget_armed) { u->forget_armed = 0; draw_dialog(u); }
            }
        } else if (type == XCB_KEY_PRESS) {
            xcb_key_press_event_t *k = (xcb_key_press_event_t *)e;
            if (u->menu && k->detail == u->key_esc) menu_destroy(u);
            else if (u->hotkey_ok && k->detail == u->key_p &&
                     (k->state & (XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1)) == (XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1)) {
                act = UI_HIDE; result = 1;
            }
        } else if (type == XCB_EXPOSE) {
            xcb_expose_event_t *x = (xcb_expose_event_t *)e;
            if (u->menu && x->window == u->menu) draw_menu(u);
            else if (u->dlg && x->window == u->dlg) draw_dialog(u);
        } else if (type == XCB_LEAVE_NOTIFY && u->dlg && u->dlg_hover != -1) { u->dlg_hover = -1; draw_dialog(u); }
        free(e);
        if (result) { *action = act; return 1; }
    }
    return 0;
}
