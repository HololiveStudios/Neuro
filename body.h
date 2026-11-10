#ifndef PESTINHA_BODY_H
#define PESTINHA_BODY_H

/* Corpo do pet na tela: fisica 2D simples (gravidade, quique, atrito, pouso no
   chao e no topo de janelas) e o "piloto" que converte as taxas dos motores da
   rede em andar, pular, fugir e ficar parado. Sem dependencia de X: testavel. */

typedef struct { unsigned long id; int x, y, w, h; } Platform;
typedef struct { int x, y, w, h; } Area;

typedef struct {
    double x, y, vx, vy;          /* canto superior esquerdo (px) e velocidade (px/s) */
    int w, h;
    double bounce;                /* restituicao ao bater no chao/parede */
    double friction;              /* decaimento de vx no chao (1/s) */
    double walk_target;           /* vx desejado no chao (px/s) */
    int dragging, grounded;
    unsigned long ground_id;      /* 0 = chao da tela, senao id da janela */
    double ground_x, ground_y;    /* posicao da plataforma no ultimo passo */
    double drag_dx, drag_dy;
    double hist_t[8], hist_x[8], hist_y[8];
    int hist_n;
} Body;

typedef struct {
    double timer, dir, hop_timer, startle_timer;
    double sprint_until, rest_until, now;
    unsigned rng;
} AiState;

typedef struct {
    double wander, cheer, startle, flee, canto;   /* taxas dos motores (Hz) */
    int asleep, sitting, corner, nook, strike;
    int pointer_valid;
    double pointer_x, pointer_y;
} MoveInputs;

void body_init(Body *b, int w, int h, double bounce, double friction);
void body_place(Body *b, double x, double y);
/* Avanca dt segundos. Devolve a velocidade de impacto (px/s) se pousou agora. */
double body_step(Body *b, double dt, const Area *work, const Platform *plats, int nplats);
void body_grab(Body *b, double px, double py, double now);
void body_drag(Body *b, double px, double py, double now);
/* Solta o corpo; velocidade de arremesso (px/s) sai em *speed. */
void body_release(Body *b, double now, double *speed);
void body_hop(Body *b, double vy);

void ai_init(AiState *ai, unsigned seed);
double ai_random(AiState *ai);                       /* 0..1 */
void ai_sprint(AiState *ai, double until, double dir);
void ai_rest(AiState *ai, double until);
/* Decide a velocidade de caminhada e os pulos deste quadro. */
void ai_step(AiState *ai, Body *b, const MoveInputs *in, const Area *work, double now, double dt);

#endif
