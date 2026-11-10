#include "fx.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_POSTERS   16
#define MAX_PAPERS    32
#define MAX_PARTICLES 128

typedef struct {
    int id;
    int x, y;
    char title[64];
    char sub[64];
    int style;
    double ttl; // time-to-live em segundos
    bool active;
} FxPosterItem;

typedef struct {
    double x, y;
    double vx, vy;
    bool active;
} FxPaperItem;

typedef struct {
    double x, y;
    double vx, vy;
    uint8_t r, g, b, alpha;
    double ttl;
    bool active;
} FxParticleItem;

typedef struct {
    bool on;
    int pet_x, pet_y, pet_w, pet_h;
} FxNotebookState;

typedef struct {
    bool enabled;
    bool held;
    double x, y;
    double vx, vy;
    double radius;
} FxBallState;

struct Fx {
    bool visible;
    int next_poster_id;

    FxPosterItem posters[MAX_POSTERS];
    FxPaperItem papers[MAX_PAPERS];
    FxParticleItem particles[MAX_PARTICLES];
    FxNotebookState notebook;
    FxBallState ball;

    FxEvent pending_event;
    bool has_event;
};

// --- Abertura e Fechamento ---

Fx *fx_open(void) {
    Fx *fx = (Fx *)calloc(1, sizeof(Fx));
    if (!fx) return NULL;

    fx->visible = true;
    fx->next_poster_id = 1;
    fx->ball.radius = 12.0;

    return fx;
}

void fx_close(Fx *fx) {
    if (fx) {
        free(fx);
    }
}

void fx_set_visible(Fx *fx, int visible) {
    if (fx) fx->visible = (visible != 0);
}

void fx_raise(Fx *fx) {
    (void)fx;
}

// --- Cartazes, Bilhetes e Placas ---

int fx_poster(Fx *fx, int x, int y, const char *title, const char *sub, int style, double ttl) {
    if (!fx) return -1;

    for (int i = 0; i < MAX_POSTERS; i++) {
        if (!fx->posters[i].active) {
            fx->posters[i].id = fx->next_poster_id++;
            fx->posters[i].x = x;
            fx->posters[i].y = y;
            fx->posters[i].style = style;
            fx->posters[i].ttl = ttl;
            fx->posters[i].active = true;

            if (title) snprintf(fx->posters[i].title, sizeof(fx->posters[i].title), "%s", title);
            else fx->posters[i].title[0] = '\0';

            if (sub) snprintf(fx->posters[i].sub, sizeof(fx->posters[i].sub), "%s", sub);
            else fx->posters[i].sub[0] = '\0';

            return fx->posters[i].id;
        }
    }
    return -1;
}

void fx_remove(Fx *fx, int id) {
    if (!fx) return;
    for (int i = 0; i < MAX_POSTERS; i++) {
        if (fx->posters[i].active && fx->posters[i].id == id) {
            fx->posters[i].active = false;
            break;
        }
    }
}

void fx_clear(Fx *fx) {
    if (!fx) return;
    for (int i = 0; i < MAX_POSTERS; i++) {
        if (fx->posters[i].style != FX_STYLE_SIGN) {
            fx->posters[i].active = false;
        }
    }
    for (int i = 0; i < MAX_PAPERS; i++) {
        fx->papers[i].active = false;
    }
}

int fx_poster_count(Fx *fx) {
    if (!fx) return 0;
    int count = 0;
    for (int i = 0; i < MAX_POSTERS; i++) {
        if (fx->posters[i].active) count++;
    }
    return count;
}

// --- Bolinhas de Papel ---

void fx_paper(Fx *fx, double x, double y, double vx, double vy) {
    if (!fx) return;
    for (int i = 0; i < MAX_PAPERS; i++) {
        if (!fx->papers[i].active) {
            fx->papers[i].x = x;
            fx->papers[i].y = y;
            fx->papers[i].vx = vx;
            fx->papers[i].vy = vy;
            fx->papers[i].active = true;
            break;
        }
    }
}

int fx_paper_count(Fx *fx) {
    if (!fx) return 0;
    int count = 0;
    for (int i = 0; i < MAX_PAPERS; i++) {
        if (fx->papers[i].active) count++;
    }
    return count;
}

// --- Fogos de Artifício ---

void fx_fireworks(Fx *fx, double x, double y, int style) {
    if (!fx) return;
    int num_sparks = 24;

    for (int k = 0; k < num_sparks; k++) {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!fx->particles[i].active) {
                double angle = ((double)rand() / RAND_MAX) * 2.0 * M_PI;
                double speed = 30.0 + ((double)rand() / RAND_MAX) * 70.0;

                fx->particles[i].x = x;
                fx->particles[i].y = y;
                fx->particles[i].vx = cos(angle) * speed;
                fx->particles[i].vy = sin(angle) * speed;
                fx->particles[i].ttl = 1.0 + ((double)rand() / RAND_MAX) * 0.5;
                fx->particles[i].active = true;

                if (style == 1) { // Vermelhos
                    fx->particles[i].r = 240;
                    fx->particles[i].g = 40;
                    fx->particles[i].b = 40;
                } else { // Coloridos
                    fx->particles[i].r = rand() % 256;
                    fx->particles[i].g = rand() % 256;
                    fx->particles[i].b = rand() % 256;
                }
                fx->particles[i].alpha = 255;
                break;
            }
        }
    }
}

int fx_particle_count(Fx *fx) {
    if (!fx) return 0;
    int count = 0;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (fx->particles[i].active) count++;
    }
    return count;
}

// --- Notebook de Trabalho ---

void fx_notebook(Fx *fx, int on, int pet_x, int pet_y, int pet_w, int pet_h) {
    if (!fx) return;
    fx->notebook.on = (on != 0);
    fx->notebook.pet_x = pet_x;
    fx->notebook.pet_y = pet_y;
    fx->notebook.pet_w = pet_w;
    fx->notebook.pet_h = pet_h;
}

// --- Bolinha Interativa ---

void fx_ball_enable(Fx *fx, int on, double x, double y) {
    if (!fx) return;
    fx->ball.enabled = (on != 0);
    fx->ball.x = x;
    fx->ball.y = y;
    fx->ball.vx = 0;
    fx->ball.vy = 0;
    fx->ball.held = false;
}

int fx_ball_enabled(Fx *fx) {
    return (fx && fx->ball.enabled) ? 1 : 0;
}

int fx_ball_held(Fx *fx) {
    return (fx && fx->ball.held) ? 1 : 0;
}

void fx_ball_center(Fx *fx, double *cx, double *cy) {
    if (!fx) return;
    if (cx) *cx = fx->ball.x;
    if (cy) *cy = fx->ball.y;
}

void fx_ball_kick(Fx *fx, double vx, double vy) {
    if (!fx || !fx->ball.enabled) return;
    fx->ball.vx = vx;
    fx->ball.vy = vy;
    fx->ball.held = false;
}

int fx_poll(Fx *fx, FxEvent *event) {
    if (!fx || !fx->has_event || !event) return 0;
    *event = fx->pending_event;
    fx->has_event = false;
    return 1;
}

// --- Atualização de Física ---

void fx_update(Fx *fx, double dt, const Area *work, const Platform *plats, int nplats) {
    (void)plats; (void)nplats;
    if (!fx) return;

    for (int i = 0; i < MAX_POSTERS; i++) {
        if (fx->posters[i].active && fx->posters[i].ttl > 0) {
            fx->posters[i].ttl -= dt;
            if (fx->posters[i].ttl <= 0) {
                fx->posters[i].active = false;
            }
        }
    }

    for (int i = 0; i < MAX_PAPERS; i++) {
        if (fx->papers[i].active) {
            fx->papers[i].vy += 300.0 * dt;
            fx->papers[i].x += fx->papers[i].vx * dt;
            fx->papers[i].y += fx->papers[i].vy * dt;

            if (work && fx->papers[i].y >= work->y + work->h - 10) {
                fx->papers[i].y = work->y + work->h - 10;
                fx->papers[i].vx *= 0.6;
                fx->papers[i].vy = -fx->papers[i].vy * 0.3;
            }
        }
    }

    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (fx->particles[i].active) {
            fx->particles[i].ttl -= dt;
            if (fx->particles[i].ttl <= 0) {
                fx->particles[i].active = false;
                continue;
            }

            fx->particles[i].x += fx->particles[i].vx * dt;
            fx->particles[i].y += fx->particles[i].vy * dt;
            fx->particles[i].vy += 100.0 * dt;

            double fade = fx->particles[i].ttl / 1.5;
            fx->particles[i].alpha = (uint8_t)(255.0 * (fade > 1.0 ? 1.0 : (fade < 0 ? 0 : fade)));
        }
    }

    if (fx->ball.enabled && !fx->ball.held) {
        fx->ball.vy += 400.0 * dt;
        fx->ball.x += fx->ball.vx * dt;
        fx->ball.y += fx->ball.vy * dt;

        if (work && fx->ball.y >= work->y + work->h - fx->ball.radius) {
            fx->ball.y = work->y + work->h - fx->ball.radius;
            fx->ball.vy = -fx->ball.vy * 0.5;
            fx->ball.vx *= 0.8;
        }
    }
}

// --- Renderização ---

void fx_draw(Fx *fx, RenderContext *render) {
    if (!fx || !fx->visible || !render) return;

    if (fx->notebook.on) {
        int nb_x = fx->notebook.pet_x + fx->notebook.pet_w;
        int nb_y = fx->notebook.pet_y + (fx->notebook.pet_h / 2);
        render_draw_rect(render, nb_x, nb_y, 24, 16, 80, 80, 90, 255);
        render_draw_rect(render, nb_x + 2, nb_y + 2, 20, 10, 30, 140, 240, 255);
    }

    for (int i = 0; i < MAX_POSTERS; i++) {
        if (fx->posters[i].active) {
            uint8_t r = 240, g = 240, b = 240;
            if (fx->posters[i].style == FX_STYLE_PROTEST) { r = 240; g = 80; b = 80; }
            else if (fx->posters[i].style == FX_STYLE_NOTE) { r = 250; g = 230; b = 100; }
            else if (fx->posters[i].style == FX_STYLE_SIGN) { r = 200; g = 140; b = 60; }

            render_draw_rect(render, fx->posters[i].x, fx->posters[i].y, 100, 40, r, g, b, 240);
        }
    }

    for (int i = 0; i < MAX_PAPERS; i++) {
        if (fx->papers[i].active) {
            render_draw_rect(render, (int)fx->papers[i].x, (int)fx->papers[i].y, 6, 6, 220, 220, 220, 255);
        }
    }

    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (fx->particles[i].active) {
            render_draw_rect(
                render,
                (int)fx->particles[i].x, (int)fx->particles[i].y, 3, 3,
                fx->particles[i].r, fx->particles[i].g, fx->particles[i].b, fx->particles[i].alpha
            );
        }
    }

    if (fx->ball.enabled) {
        int bx = (int)(fx->ball.x - fx->ball.radius);
        int by = (int)(fx->ball.y - fx->ball.radius);
        render_draw_rect(render, bx, by, 16, 16, 80, 220, 80, 255);
    }
}
