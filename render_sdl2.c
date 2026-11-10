/*
 * render_sdl2.c — Backend SDL2 para render.h.
 *
 * Funciona em:
 *   Linux  X11      (SDL_VIDEODRIVER=x11)
 *   Linux  Wayland  (SDL_VIDEODRIVER=wayland)
 *   Windows         (compilado com MinGW ou MSVC)
 *   macOS           (sem alteração)
 *
 * SDL2 abstrai a janela, os eventos e o blit.
 * Este arquivo não tem nenhuma linha de código X11/XCB.
 *
 * Compilar Linux:   -lSDL2
 * Compilar Windows: -lSDL2 -lSDL2main -mwindows
 *
 * GPL-3.0-or-later
 */
#include "render.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Estado interno                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *tex;
    int           w, h;
} Sdl2State;

static Sdl2State S;

/* ------------------------------------------------------------------ */
/* init                                                                */
/* ------------------------------------------------------------------ */

static int sdl2_init(const RenderConfig *cfg)
{
    Uint32 win_flags = SDL_WINDOW_SHOWN;

    memset(&S, 0, sizeof S);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "render_sdl2: SDL_Init falhou: %s\n", SDL_GetError());
        return 0;
    }

    S.w = cfg->w;
    S.h = cfg->h;

    /* Modo desktop: sem borda e sem barra de título */
    if (cfg->desktop)
        win_flags |= SDL_WINDOW_BORDERLESS;

    S.win = SDL_CreateWindow(
        cfg->title ? cfg->title : "Pestinha",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        cfg->w, cfg->h,
        win_flags);

    if (!S.win) {
        fprintf(stderr, "render_sdl2: SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 0;
    }

    /* Renderer acelerado se disponível, senão software */
    S.ren = SDL_CreateRenderer(S.win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!S.ren)
        S.ren = SDL_CreateRenderer(S.win, -1, SDL_RENDERER_SOFTWARE);
    if (!S.ren) {
        fprintf(stderr, "render_sdl2: SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(S.win);
        SDL_Quit();
        return 0;
    }

    /*
     * Textura streaming: atualizada a cada frame com SDL_UpdateTexture.
     * SDL_PIXELFORMAT_ARGB8888 = 0xAARRGGBB em uint32_t, coincide com
     * o formato gfx_rgb() = 0x00RRGGBB (alpha 0 é ignorado).
     */
    S.tex = SDL_CreateTexture(S.ren,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        cfg->w, cfg->h);
    if (!S.tex) {
        fprintf(stderr, "render_sdl2: SDL_CreateTexture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(S.ren);
        SDL_DestroyWindow(S.win);
        SDL_Quit();
        return 0;
    }

    return 1;
}

/* ------------------------------------------------------------------ */
/* shutdown                                                            */
/* ------------------------------------------------------------------ */

static void sdl2_shutdown(void)
{
    if (S.tex) SDL_DestroyTexture(S.tex);
    if (S.ren) SDL_DestroyRenderer(S.ren);
    if (S.win) SDL_DestroyWindow(S.win);
    SDL_Quit();
    memset(&S, 0, sizeof S);
}

/* ------------------------------------------------------------------ */
/* poll                                                                */
/* ------------------------------------------------------------------ */

static int sdl2_poll(RenderEvent *out, int max_ev)
{
    int count = 0;
    SDL_Event ev;

    while (count < max_ev && SDL_PollEvent(&ev)) {
        RenderEvent *re = &out[count];
        re->type   = RENDER_EV_NONE;
        re->x      = 0;
        re->y      = 0;
        re->button = 0;
        re->key    = 0;

        switch (ev.type) {

        case SDL_QUIT:
            re->type = RENDER_EV_CLOSE;
            break;

        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_EXPOSED)
                re->type = RENDER_EV_EXPOSE;
            break;

        case SDL_MOUSEMOTION:
            re->type = RENDER_EV_MOUSE_MOVE;
            re->x    = ev.motion.x;
            re->y    = ev.motion.y;
            break;

        case SDL_MOUSEBUTTONDOWN:
            re->type   = RENDER_EV_MOUSE_DOWN;
            re->x      = ev.button.x;
            re->y      = ev.button.y;
            re->button = ev.button.button; /* SDL_BUTTON_LEFT = 1, etc. */
            break;

        case SDL_MOUSEBUTTONUP:
            re->type   = RENDER_EV_MOUSE_UP;
            re->x      = ev.button.x;
            re->y      = ev.button.y;
            re->button = ev.button.button;
            break;

        case SDL_KEYDOWN:
            re->type = RENDER_EV_KEY_DOWN;
            re->key  = ev.key.keysym.scancode;
            break;

        case SDL_KEYUP:
            re->type = RENDER_EV_KEY_UP;
            re->key  = ev.key.keysym.scancode;
            break;

        default:
            break;
        }

        if (re->type != RENDER_EV_NONE)
            ++count;
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* present                                                             */
/* ------------------------------------------------------------------ */

static void sdl2_present(const uint32_t *pixels, int w, int h,
                         const int dirty[4])
{
    int pitch = w * (int)sizeof(uint32_t);

    if (dirty && dirty[2] > 0 && dirty[3] > 0) {
        /*
         * Atualiza só o dirty rect.
         * O ponteiro aponta para o início da primeira linha do rect
         * dentro do buffer completo; pitch continua sendo a largura
         * total (stride da fonte).
         */
        SDL_Rect r = { dirty[0], dirty[1], dirty[2], dirty[3] };
        const uint8_t *src = (const uint8_t *)pixels
                           + (size_t)dirty[1] * pitch
                           + (size_t)dirty[0] * sizeof(uint32_t);
        SDL_UpdateTexture(S.tex, &r, src, pitch);
    } else {
        SDL_UpdateTexture(S.tex, NULL, pixels, pitch);
    }

    SDL_RenderClear(S.ren);
    SDL_RenderCopy(S.ren, S.tex, NULL, NULL);
    SDL_RenderPresent(S.ren);
    (void)h;
}

/* ------------------------------------------------------------------ */
/* move / sizes                                                        */
/* ------------------------------------------------------------------ */

static void sdl2_move(int x, int y)
{
    if (S.win) SDL_SetWindowPosition(S.win, x, y);
}

static void sdl2_window_size(int *w, int *h)
{
    if (S.win) SDL_GetWindowSize(S.win, w, h);
    else       { *w = S.w; *h = S.h; }
}

static void sdl2_screen_size(int *w, int *h)
{
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
        *w = dm.w; *h = dm.h;
    } else {
        *w = *h = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Backend exportado                                                   */
/* ------------------------------------------------------------------ */

RenderBackend render_backend_sdl2 = {
    .name        = "sdl2",
    .init        = sdl2_init,
    .shutdown    = sdl2_shutdown,
    .poll        = sdl2_poll,
    .present     = sdl2_present,
    .move        = sdl2_move,
    .window_size = sdl2_window_size,
    .screen_size = sdl2_screen_size,
};
