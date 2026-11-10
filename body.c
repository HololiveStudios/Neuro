#include "body.h"

#include <math.h>
#include <stddef.h>

#define GRAVITY 1900.0
#define MAX_THROW 2800.0

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

void body_init(Body *b, int w, int h, double bounce, double friction)
{
    int i;
    b->x = b->y = b->vx = b->vy = 0.0;
    b->w = w; b->h = h;
    b->bounce = bounce; b->friction = friction;
    b->walk_target = 0.0;
    b->dragging = b->grounded = 0;
    b->ground_id = 0;
    b->ground_x = b->ground_y = 0.0;
    b->drag_dx = b->drag_dy = 0.0;
    b->hist_n = 0;
    for (i = 0; i < 8; ++i) b->hist_t[i] = b->hist_x[i] = b->hist_y[i] = 0.0;
}

void body_place(Body *b, double x, double y)
{
    b->x = x; b->y = y; b->vx = b->vy = 0.0;
    b->grounded = 0; b->ground_id = 0;
}

static const Platform *find_platform(const Platform *p, int n, unsigned long id)
{
    int i;
    for (i = 0; i < n; ++i) if (p[i].id == id) return &p[i];
    return NULL;
}

double body_step(Body *b, double dt, const Area *work, const Platform *plats, int nplats)
{
    double impact = 0.0, feet, prev_feet, cx, floor_y, land_y;
    unsigned long land_id;
    int land = 0, i;
    if (b->dragging) return 0.0;

    if (b->grounded) {
        if (b->ground_id) {
            const Platform *p = find_platform(plats, nplats, b->ground_id);
            if (!p) b->grounded = 0;
            else {
                b->x += p->x - b->ground_x;
                b->y = p->y - b->h;
                b->ground_x = p->x; b->ground_y = p->y;
            }
        }
        if (b->grounded) {
            double target = b->walk_target;
            b->vx += (target - b->vx) * (1.0 - exp(-dt * (target == 0.0 ? b->friction : 9.0)));
            if (fabs(b->vx) < 0.5 && target == 0.0) b->vx = 0.0;
            b->vy = 0.0;
        }
    }
    if (!b->grounded) b->vy += GRAVITY * dt;

    b->x += b->vx * dt;
    b->y += b->vy * dt;
    if (b->grounded && b->ground_id) b->ground_x += 0.0;

    if (b->x < work->x) { b->x = work->x; b->vx = -b->vx * b->bounce; }
    if (b->x > work->x + work->w - b->w) { b->x = work->x + work->w - b->w; b->vx = -b->vx * b->bounce; }
    if (b->y < work->y) { b->y = work->y; if (b->vy < 0) b->vy = -b->vy * b->bounce; }

    cx = b->x + b->w * 0.5;
    if (b->grounded && b->ground_id) {
        const Platform *p = find_platform(plats, nplats, b->ground_id);
        if (p && (cx < p->x || cx > p->x + p->w)) b->grounded = 0;
    }
    if (b->grounded) {
        double fl = work->y + work->h - b->h;
        if (!b->ground_id && b->y > fl) b->y = fl;
        if (b->ground_id) { /* mantem os pes na plataforma */ }
        return 0.0;
    }

    feet = b->y + b->h;
    prev_feet = feet - b->vy * dt;
    floor_y = work->y + work->h;
    land_y = floor_y; land_id = 0;
    if (b->vy >= 0.0 && feet >= floor_y) land = 1;
    for (i = 0; i < nplats; ++i) {
        const Platform *p = &plats[i];
        if (b->vy < 0.0) break;
        if (cx < p->x || cx > p->x + p->w) continue;
        if (prev_feet <= p->y + 3.0 && feet >= p->y && p->y < land_y) {
            land = 1; land_y = p->y; land_id = p->id;
        }
    }
    if (land) {
        double v = b->vy;
        b->y = land_y - b->h;
        impact = v;
        b->vy = -v * b->bounce;
        if (fabs(b->vy) < 140.0) {
            const Platform *p = land_id ? find_platform(plats, nplats, land_id) : NULL;
            b->vy = 0.0; b->grounded = 1; b->ground_id = land_id;
            b->ground_x = p ? p->x : 0.0; b->ground_y = p ? p->y : 0.0;
        }
    }
    return impact;
}

void body_grab(Body *b, double px, double py, double now)
{
    b->dragging = 1; b->grounded = 0; b->ground_id = 0;
    b->drag_dx = px - b->x; b->drag_dy = py - b->y;
    b->vx = b->vy = 0.0; b->hist_n = 0;
    b->hist_t[0] = now; b->hist_x[0] = px; b->hist_y[0] = py; b->hist_n = 1;
}

void body_drag(Body *b, double px, double py, double now)
{
    int i, keep = 0;
    if (!b->dragging) return;
    b->x = px - b->drag_dx; b->y = py - b->drag_dy;
    for (i = 0; i < b->hist_n; ++i)
        if (now - b->hist_t[i] <= 0.15) {
            b->hist_t[keep] = b->hist_t[i]; b->hist_x[keep] = b->hist_x[i];
            b->hist_y[keep] = b->hist_y[i]; ++keep;
        }
    b->hist_n = keep;
    if (b->hist_n >= 8) {
        for (i = 1; i < 8; ++i) {
            b->hist_t[i - 1] = b->hist_t[i]; b->hist_x[i - 1] = b->hist_x[i];
            b->hist_y[i - 1] = b->hist_y[i];
        }
        b->hist_n = 7;
    }
    b->hist_t[b->hist_n] = now; b->hist_x[b->hist_n] = px; b->hist_y[b->hist_n] = py;
    ++b->hist_n;
}

void body_release(Body *b, double now, double *speed)
{
    double vx = 0.0, vy = 0.0;
    int i, keep = 0;
    for (i = 0; i < b->hist_n; ++i)
        if (now - b->hist_t[i] <= 0.15) {
            b->hist_t[keep] = b->hist_t[i]; b->hist_x[keep] = b->hist_x[i];
            b->hist_y[keep] = b->hist_y[i]; ++keep;
        }
    b->hist_n = keep;
    if (keep >= 2 && b->hist_t[keep - 1] - b->hist_t[0] >= 0.03) {
        double span = b->hist_t[keep - 1] - b->hist_t[0];
        vx = (b->hist_x[keep - 1] - b->hist_x[0]) / span;
        vy = (b->hist_y[keep - 1] - b->hist_y[0]) / span;
    }
    vx = clampd(vx, -MAX_THROW, MAX_THROW);
    vy = clampd(vy, -MAX_THROW, MAX_THROW);
    b->dragging = 0; b->grounded = 0; b->ground_id = 0;
    b->vx = vx; b->vy = vy;
    if (speed) *speed = sqrt(vx * vx + vy * vy);
}

void body_hop(Body *b, double vy)
{
    if (b->dragging) return;
    b->vy = -fabs(vy); b->grounded = 0; b->ground_id = 0;
}

/* ---- piloto ---- */

void ai_init(AiState *ai, unsigned seed)
{
    ai->timer = ai->dir = ai->hop_timer = ai->startle_timer = 0.0;
    ai->sprint_until = ai->rest_until = ai->now = 0.0;
    ai->rng = seed ? seed : 2463534242u;
}

double ai_random(AiState *ai)
{
    unsigned x = ai->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    ai->rng = x ? x : 2463534242u;
    return (double)(ai->rng >> 8) / 16777216.0;
}

void ai_sprint(AiState *ai, double until, double dir) { ai->sprint_until = until; ai->dir = dir < 0 ? -1.0 : 1.0; }
void ai_rest(AiState *ai, double until) { ai->rest_until = until; }

void ai_step(AiState *ai, Body *b, const MoveInputs *in, const Area *work, double now, double dt)
{
    double target = 0.0, cx = b->x + b->w * 0.5;
    ai->now = now;
    ai->hop_timer -= dt; ai->startle_timer -= dt; ai->timer -= dt;
    if (b->dragging) { b->walk_target = 0.0; return; }

    if (in->asleep || in->sitting || in->strike || now < ai->rest_until) {
        target = 0.0;
    } else if (in->corner) {
        double goal = work->x + work->w - b->w - 6.0;
        double d = goal - b->x;
        target = fabs(d) < 4.0 ? 0.0 : (d > 0 ? 170.0 : -170.0);
    } else if (now < ai->sprint_until) {
        target = ai->dir * 430.0;
        if (b->x <= work->x + 1.0) ai->dir = 1.0;
        if (b->x >= work->x + work->w - b->w - 1.0) ai->dir = -1.0;
    } else if (in->flee >= 4.0 && in->pointer_valid && fabs(cx - in->pointer_x) < 520.0) {
        double away = cx >= in->pointer_x ? 1.0 : -1.0;
        if ((away > 0 && b->x >= work->x + work->w - b->w - 2.0) ||
            (away < 0 && b->x <= work->x + 2.0)) {
            target = 0.0;
            if (b->grounded && ai->hop_timer <= 0.0) { body_hop(b, 520.0); ai->hop_timer = 0.8; }
        } else target = away * clampd(150.0 + in->flee * 12.0, 150.0, 260.0);
    } else if (in->nook) {
        double goal = work->x + 6.0, d = goal - b->x;
        target = fabs(d) < 4.0 ? 0.0 : (d > 0 ? 90.0 : -90.0);
    } else if (in->wander >= 3.0) {
        if (ai->timer <= 0.0) {
            double r = ai_random(ai);
            ai->dir = r < 0.35 ? 0.0 : (r < 0.675 ? -1.0 : 1.0);
            ai->timer = 1.5 + 4.0 * ai_random(ai);
        }
        target = ai->dir * clampd(45.0 + in->wander * 4.0, 45.0, 120.0);
        if (b->x <= work->x + 1.0) ai->dir = 1.0;
        if (b->x >= work->x + work->w - b->w - 1.0) ai->dir = -1.0;
    }
    b->walk_target = target;

    if (!in->asleep && !in->sitting && !in->strike && !in->corner && b->grounded) {
        if (in->startle >= 7.0 && ai->startle_timer <= 0.0) {
            body_hop(b, 640.0); ai->startle_timer = 1.5;
        } else if (in->cheer >= 4.0 && ai->hop_timer <= 0.0) {
            body_hop(b, clampd(360.0 + in->cheer * 8.0, 360.0, 540.0));
            ai->hop_timer = 0.9 + 0.7 * ai_random(ai);
        }
    }
}
