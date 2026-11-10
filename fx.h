#ifndef PESTINHA_FX_H
#define PESTINHA_FX_H

#include "body.h"
#include <stdint.h>

/* Forward declaration do contexto de renderização */
typedef struct RenderContext RenderContext;

/* Declaração da função de desenho utilizada pelo fx.c */
void render_draw_rect(RenderContext* ctx, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

/* Efeitos visuais do modo desktop... */
typedef struct Fx Fx;
typedef enum { FX_NONE = 0, FX_BALL_GRAB, FX_BALL_THROW } FxEventType;
typedef struct { int type; double speed; } FxEvent;

enum { FX_STYLE_PROTEST = 0, FX_STYLE_NOTE = 1, FX_STYLE_SIGN = 2, FX_STYLE_PRANK = 3 };

Fx *fx_open(void);
void fx_close(Fx *fx);
void fx_set_visible(Fx *fx, int visible);
void fx_raise(Fx *fx);

int  fx_poster(Fx *fx, int x, int y, const char *title, const char *sub, int style, double ttl);
void fx_remove(Fx *fx, int id);
void fx_clear(Fx *fx);
int  fx_poster_count(Fx *fx);
void fx_paper(Fx *fx, double x, double y, double vx, double vy);
int  fx_paper_count(Fx *fx);
void fx_fireworks(Fx *fx, double x, double y, int style);
int  fx_particle_count(Fx *fx);
void fx_notebook(Fx *fx, int on, int pet_x, int pet_y, int pet_w, int pet_h);

void fx_ball_enable(Fx *fx, int on, double x, double y);
int  fx_ball_enabled(Fx *fx);
int  fx_ball_held(Fx *fx);
void fx_ball_center(Fx *fx, double *cx, double *cy);
void fx_ball_kick(Fx *fx, double vx, double vy);

int  fx_poll(Fx *fx, FxEvent *event);
void fx_update(Fx *fx, double dt, const Area *work, const Platform *plats, int nplats);

/* Desenha a camada de efeitos na SDL2 */
void fx_draw(Fx *fx, RenderContext *render);

#endif // PESTINHA_FX_H
