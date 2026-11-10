#define _POSIX_C_SOURCE 200809L
#include "sensors.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>

#define MAX_KNOWN_APPS 128
#define MAX_APP_NAME 64

static xcb_extension_t ss_ext = {"MIT-SCREEN-SAVER", 0};

typedef struct {
    uint8_t major_opcode;
    uint8_t minor_opcode;
    uint16_t length;
    uint32_t drawable;
} ScreenSaverQueryInfoReq;

typedef struct {
    uint8_t response_type;
    uint8_t state;
    uint16_t sequence;
    uint32_t length;
    xcb_window_t window;
    uint32_t til_or_since;
    uint32_t idle;
    uint32_t event_mask;
    uint8_t kind;
    uint8_t pad[7];
} ScreenSaverQueryInfoReply;

struct Sensors {
    xcb_connection_t *conn;
    xcb_window_t root;
    xcb_atom_t atom_active_win;
    xcb_atom_t atom_wm_class;
    int has_screensaver;
    uint8_t ss_major;

    double last_t;
    double last_activity_t;

    double pointer_x, pointer_y;
    int pointer_initialized;
    double motion;
    double typing;
    double sound;
    double work;
    double web;
    double novelty;

    char known_apps[MAX_KNOWN_APPS][MAX_APP_NAME];
    int num_known_apps;
};

static void str_tolower(char *dst, const char *src, size_t maxlen)
{
    size_t i;
    for (i = 0; i + 1 < maxlen && src[i]; ++i) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static xcb_atom_t intern_atom(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_cookie_t ck = xcb_intern_atom(c, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, ck, NULL);
    xcb_atom_t atom = r ? r->atom : XCB_ATOM_NONE;
    free(r);
    return atom;
}

static int check_asound_running_rec(const char *path, int depth)
{
    DIR *d;
    struct dirent *ent;
    if (depth > 4) return 0;
    d = opendir(path);
    if (!d) return 0;

    while ((ent = readdir(d)) != NULL) {
        char subpath[512];
        struct stat st;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        snprintf(subpath, sizeof subpath, "%s/%s", path, ent->d_name);
        if (stat(subpath, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (check_asound_running_rec(subpath, depth + 1)) {
                closedir(d);
                return 1;
            }
        } else if (strcmp(ent->d_name, "status") == 0) {
            FILE *f = fopen(subpath, "r");
            if (f) {
                char buf[256];
                size_t n = fread(buf, 1, sizeof(buf) - 1, f);
                fclose(f);
                buf[n] = '\0';
                if (strstr(buf, "RUNNING") != NULL) {
                    closedir(d);
                    return 1;
                }
            }
        }
    }
    closedir(d);
    return 0;
}

static int is_sound_active(void)
{
    const char *base = getenv("PESTINHA_ASOUND");
    if (!base || !*base) base = "/proc/asound";
    return check_asound_running_rec(base, 0);
}

Sensors *sensors_open(void)
{
    Sensors *s = calloc(1, sizeof *s);
    int sn = 0;
    const xcb_setup_t *setup;
    xcb_screen_iterator_t it;
    const xcb_query_extension_reply_t *ext;

    if (!s) return NULL;
    s->conn = xcb_connect(NULL, &sn);
    if (!s->conn || xcb_connection_has_error(s->conn)) {
        free(s);
        return NULL;
    }

    setup = xcb_get_setup(s->conn);
    it = xcb_setup_roots_iterator(setup);
    while (sn-- > 0) xcb_screen_next(&it);
    s->root = it.data->root;

    s->atom_active_win = intern_atom(s->conn, "_NET_ACTIVE_WINDOW");
    s->atom_wm_class = XCB_ATOM_WM_CLASS;

    ext = xcb_get_extension_data(s->conn, &ss_ext);
    if (ext && ext->present) {
        s->has_screensaver = 1;
        s->ss_major = ext->major_opcode;
    }

    return s;
}

void sensors_close(Sensors *s)
{
    if (!s) return;
    if (s->conn) xcb_disconnect(s->conn);
    free(s);
}

void sensors_poll(Sensors *s, double now, SensorReading *out)
{
    double dt;
    int motion_detected = 0;
    int typing_detected = 0;
    double current_idle = 0.0;
    xcb_query_pointer_cookie_t ptr_ck;
    xcb_query_pointer_reply_t *ptr_r;
    double target_work = 0.0, target_web = 0.0;
    double target_sound = 0.0;

    if (!s || !out) return;

    if (s->last_t <= 0.0) {
        s->last_t = now;
        s->last_activity_t = now;
        dt = 0.033;
    } else {
        dt = now - s->last_t;
        if (dt < 0.0001) dt = 0.0001;
        if (dt > 1.0) dt = 1.0;
        s->last_t = now;
    }

    /* 1. Consulta ponteiro do mouse */
    ptr_ck = xcb_query_pointer(s->conn, s->root);
    ptr_r = xcb_query_pointer_reply(s->conn, ptr_ck, NULL);
    if (ptr_r) {
        double px = (double)ptr_r->root_x;
        double py = (double)ptr_r->root_y;
        if (s->pointer_initialized) {
            double dx = px - s->pointer_x;
            double dy = py - s->pointer_y;
            double dist = sqrt(dx * dx + dy * dy);
            if (dist > 2.0) {
                motion_detected = 1;
                s->last_activity_t = now;
            }
        } else {
            s->pointer_initialized = 1;
        }
        s->pointer_x = px;
        s->pointer_y = py;
        free(ptr_r);
    }

    /* 2. Consulta tempo ocioso nativo via MIT-SCREEN-SAVER ou relógio interno */
    if (s->has_screensaver) {
        ScreenSaverQueryInfoReq req = { s->ss_major, 1, 2, s->root };
        struct iovec parts[3];
        parts[2].iov_base = &req;
        parts[2].iov_len = sizeof req;
        static const xcb_protocol_request_t pr = { 1, &ss_ext, 1, 1 };
        unsigned int ck = xcb_send_request(s->conn, 0, parts + 2, &pr);
        ScreenSaverQueryInfoReply *rep = (ScreenSaverQueryInfoReply *)xcb_wait_for_reply(s->conn, ck, NULL);
        if (rep) {
            if (rep->idle < 120 && !motion_detected) {
                typing_detected = 1;
                s->last_activity_t = now;
            }
            free(rep);
        }
    }
    current_idle = now - s->last_activity_t;
    if (current_idle < 0.0) current_idle = 0.0;

    /* 3. Atualização de movimento e digitação com EMA */
    {
        double inst_motion = motion_detected ? 1.0 : 0.0;
        double alpha_m = 1.0 - exp(-dt * (motion_detected ? 15.0 : 1.5));
        s->motion += (inst_motion - s->motion) * alpha_m;

        double inst_typing = typing_detected ? 1.0 : 0.0;
        double alpha_t = 1.0 - exp(-dt * (typing_detected ? 15.0 : 1.2));
        s->typing += (inst_typing - s->typing) * alpha_t;
    }

    /* 4. Janela ativa e classificação (Work, Web, Novelty) */
    if (s->atom_active_win != XCB_ATOM_NONE) {
        xcb_get_property_cookie_t ack = xcb_get_property(
            s->conn, 0, s->root, s->atom_active_win, XCB_ATOM_WINDOW, 0, 1);
        xcb_get_property_reply_t *ar = xcb_get_property_reply(s->conn, ack, NULL);
        if (ar && ar->format == 32 && xcb_get_property_value_length(ar) >= 4) {
            xcb_window_t active_win = *(const xcb_window_t *)xcb_get_property_value(ar);
            if (active_win != XCB_WINDOW_NONE) {
                xcb_get_property_cookie_t cck = xcb_get_property(
                    s->conn, 0, active_win, s->atom_wm_class, XCB_ATOM_STRING, 0, 64);
                xcb_get_property_reply_t *cr = xcb_get_property_reply(s->conn, cck, NULL);
                if (cr && cr->format == 8 && xcb_get_property_value_length(cr) > 0) {
                    const char *raw = (const char *)xcb_get_property_value(cr);
                    int len = xcb_get_property_value_length(cr);
                    char buf[128];
                    char lower[128];
                    int copy_len = len < 127 ? len : 127;
                    int i;
                    memcpy(buf, raw, copy_len);
                    buf[copy_len] = '\0';
                    for (i = 0; i < copy_len; ++i) if (buf[i] == '\0') buf[i] = ' ';
                    str_tolower(lower, buf, sizeof lower);

                    if (strstr(lower, "firefox") || strstr(lower, "chrome") ||
                        strstr(lower, "chromium") || strstr(lower, "brave") ||
                        strstr(lower, "edge") || strstr(lower, "opera") || strstr(lower, "browser")) {
                        target_web = 1.0;
                    } else if (strstr(lower, "code") || strstr(lower, "visual studio") ||
                               strstr(lower, "gedit") || strstr(lower, "kate") ||
                               strstr(lower, "sublime") || strstr(lower, "vim") ||
                               strstr(lower, "terminal") || strstr(lower, "idea") ||
                               strstr(lower, "blender") || strstr(lower, "figma")) {
                        target_work = 1.0;
                    }

                    if (lower[0] != '\0') {
                        int known = 0;
                        for (i = 0; i < s->num_known_apps; ++i) {
                            if (strcmp(s->known_apps[i], lower) == 0) {
                                known = 1;
                                break;
                            }
                        }
                        if (!known) {
                            if (s->num_known_apps < MAX_KNOWN_APPS) {
                                size_t len_app = strlen(lower);
                                if (len_app >= MAX_APP_NAME) len_app = MAX_APP_NAME - 1;
                                memcpy(s->known_apps[s->num_known_apps], lower, len_app);
                                s->known_apps[s->num_known_apps][len_app] = '\0';
                                s->num_known_apps++;
                            }
                            s->novelty = 1.0;
                        }
                    }
                } else {
                    /* printf("cr failed or len 0 for win 0x%x\n", active_win); */
                }
                free(cr);
            }
        } else {
            /* printf("ar format %d len %d\n", ar ? ar->format : -1, ar ? xcb_get_property_value_length(ar) : -1); */
        }
        free(ar);
    }

    /* Suavização de Work e Web */
    {
        double alpha_w = 1.0 - exp(-dt * 6.0);
        s->work += (target_work - s->work) * alpha_w;
        s->web += (target_web - s->web) * alpha_w;
        s->novelty *= exp(-dt * 0.4);
    }

    /* 5. Áudio (Sound) */
    if (is_sound_active()) {
        target_sound = 1.0;
    }
    {
        double alpha_s = 1.0 - exp(-dt * 2.0);
        s->sound += (target_sound - s->sound) * alpha_s;
    }

    /* Preenche resultado */
    out->work = s->work;
    out->web = s->web;
    out->novelty = s->novelty;
    out->idle_seconds = current_idle;
    out->motion = s->motion;
    out->pointer_x = s->pointer_x;
    out->pointer_y = s->pointer_y;
    out->typing = s->typing;
    out->sound = s->sound;
}
