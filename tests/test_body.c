/* Teste da fisica e do piloto, sem X. Compilar: make test */
#include "../body.h"
#include <math.h>
#include <stdio.h>
static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FALHA: %s\n", msg); ++fails; } else printf("ok: %s\n", msg); } while (0)
int main(void)
{
    Body b; AiState ai; Area work = {0, 0, 1280, 720}; MoveInputs in = {0};
    Platform plats[2] = {{101, 300, 500, 400, 300}, {102, 900, 500, 300, 200}};
    double t, impact = 0, max_imp = 0, speed; int i, bounced = 0;

    /* 1. queda livre ate o chao, com quique que morre */
    body_init(&b, 120, 90, 0.35, 8.0); body_place(&b, 100, 50);
    for (i = 0; i < 600; ++i) { double im = body_step(&b, 1 / 60.0, &work, NULL, 0); if (im > 0) { ++bounced; if (im > max_imp) max_imp = im; } }
    CHECK(b.grounded && fabs(b.y + b.h - 720) < 0.5, "cai e para exatamente no chao");
    CHECK(bounced >= 2 && max_imp > 900, "quica algumas vezes antes de parar");

    /* 2. pousa no topo de uma janela e a acompanha quando ela se move */
    body_init(&b, 120, 90, 0.35, 8.0); body_place(&b, 450, 100);
    for (i = 0; i < 300; ++i) body_step(&b, 1 / 60.0, &work, plats, 2);
    CHECK(b.grounded && b.ground_id == 101 && fabs(b.y + b.h - 500) < 0.5, "pousa no topo da janela 101");
    plats[0].x += 60; plats[0].y -= 40;
    for (i = 0; i < 5; ++i) body_step(&b, 1 / 60.0, &work, plats, 2);
    CHECK(fabs(b.y + b.h - 460) < 0.5 && b.x > 480, "acompanha a janela quando ela e arrastada");

    /* 3. a janela some: ele cai ate o chao */
    for (i = 0; i < 300; ++i) body_step(&b, 1 / 60.0, &work, NULL, 0);
    CHECK(b.grounded && b.ground_id == 0 && fabs(b.y + b.h - 720) < 0.5, "janela fechada: cai para o chao");

    /* 4. andar para fora da borda da janela faz cair */
    plats[0].x -= 60; plats[0].y += 40;
    body_init(&b, 120, 90, 0.35, 8.0); body_place(&b, 330, 100);
    for (i = 0; i < 200; ++i) body_step(&b, 1 / 60.0, &work, plats, 2);
    b.walk_target = 120;
    for (i = 0; i < 600 && b.ground_id == 101; ++i) body_step(&b, 1 / 60.0, &work, plats, 2);
    CHECK(i < 600 && !(b.grounded && b.ground_id == 101), "andou ate a borda e deixou a janela");

    /* 5. arrastar e arremessar: a velocidade de soltura vem do movimento do mouse */
    body_init(&b, 120, 90, 0.35, 8.0); body_place(&b, 500, 600);
    body_grab(&b, 520, 620, 0.00);
    for (t = 0.01; t <= 0.12; t += 0.01) body_drag(&b, 520 + 3000 * t, 620 - 1500 * t, t);
    body_release(&b, 0.12, &speed);
    CHECK(speed > 2500 && speed <= 2800 * 1.5 && b.vx > 0 && b.vy < 0, "soltar em movimento rapido arremessa para cima e a direita");
    for (i = 0; i < 180; ++i) body_step(&b, 1 / 60.0, &work, NULL, 0);
    CHECK(b.x <= work.x + work.w - b.w + 0.01 && b.x >= 0, "nao sai pela lateral da tela");

    /* 6. soltar parado nao arremessa */
    body_init(&b, 120, 90, 0.35, 8.0); body_place(&b, 500, 300);
    body_grab(&b, 520, 320, 0.0); body_drag(&b, 521, 320, 0.2); body_release(&b, 0.5, &speed);
    CHECK(speed < 1.0, "soltar depois de ficar parado nao arremessa");

    /* 7. piloto: vagar anda, sentado/dormindo/greve ficam parados, fuga corre para longe do mouse */
    body_init(&b, 120, 90, 0.35, 8.0); body_place(&b, 600, 630); b.grounded = 1;
    ai_init(&ai, 7);
    in.wander = 10; t = 0; { double x0 = b.x, span = 0;
        for (i = 0; i < 1800; ++i) { t += 1 / 60.0; ai_step(&ai, &b, &in, &work, t, 1 / 60.0); body_step(&b, 1 / 60.0, &work, NULL, 0); span += fabs(b.vx) / 60.0; }
        CHECK(span > 150 && b.x != x0, "com WANDER alto ele anda"); }
    in.sitting = 1; for (i = 0; i < 120; ++i) { t += 1 / 60.0; ai_step(&ai, &b, &in, &work, t, 1 / 60.0); body_step(&b, 1 / 60.0, &work, NULL, 0); }
    CHECK(fabs(b.vx) < 1.0, "sentado ele fica parado"); in.sitting = 0;
    in.strike = 1; for (i = 0; i < 120; ++i) { t += 1 / 60.0; ai_step(&ai, &b, &in, &work, t, 1 / 60.0); body_step(&b, 1 / 60.0, &work, NULL, 0); }
    CHECK(fabs(b.vx) < 1.0, "em greve ele fica parado"); in.strike = 0; in.wander = 0;
    body_place(&b, 600, 630); b.grounded = 1; in.flee = 10; in.pointer_valid = 1; in.pointer_x = 500;
    for (i = 0; i < 120; ++i) { t += 1 / 60.0; ai_step(&ai, &b, &in, &work, t, 1 / 60.0); body_step(&b, 1 / 60.0, &work, NULL, 0); }
    CHECK(b.x > 800, "FUGIR alto: corre para longe do mouse");
    in.flee = 0; in.corner = 1;
    for (i = 0; i < 900; ++i) { t += 1 / 60.0; ai_step(&ai, &b, &in, &work, t, 1 / 60.0); body_step(&b, 1 / 60.0, &work, NULL, 0); }
    CHECK(fabs(b.x - (work.x + work.w - b.w - 6)) < 6, "no castigo ele vai para o canto direito e fica la");
    in.corner = 0; in.cheer = 10; { int hops = 0; double prev_vy = 0;
        for (i = 0; i < 600; ++i) { t += 1 / 60.0; ai_step(&ai, &b, &in, &work, t, 1 / 60.0); body_step(&b, 1 / 60.0, &work, NULL, 0); if (b.vy < -300 && prev_vy >= -300) ++hops; prev_vy = b.vy; }
        CHECK(hops >= 4, "CHEER alto: da pulinhos de alegria"); }
    printf(fails ? "\n%d falha(s)\n" : "\ntodos os testes passaram\n", fails);
    return fails != 0;
}
