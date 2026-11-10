#include "display.h"
#include <stdio.h>
#include <stdlib.h>

DisplayContext* display_init(const char* title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "[DISPLAY] Erro ao inicializar SDL: %s\n", SDL_GetError());
        return NULL;
    }

    // Força o compositor do Linux (X11/Wayland) a manter o canal alfa de transparência ativo
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

    DisplayContext* ctx = (DisplayContext*)malloc(sizeof(DisplayContext));
    if (!ctx) return NULL;

    // Obtém a resolução da tela principal
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
        ctx->screen_width = dm.w;
        ctx->screen_height = dm.h;
    } else {
        ctx->screen_width = 1920;
        ctx->screen_height = 1080;
    }

    // Cria janela transparente, sem bordas e no topo
    ctx->window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_SKIP_TASKBAR
    );

    if (!ctx->window) {
        fprintf(stderr, "[DISPLAY] Erro ao criar janela: %s\n", SDL_GetError());
        free(ctx);
        SDL_Quit();
        return NULL;
    }

    return ctx;
}

void display_poll_events(DisplayContext* ctx, bool* running) {
    (void)ctx;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            *running = false;
        } else if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                *running = false;
            }
        }
    }
}

// Obtém a posição global do cursor na tela (substitui xcb_query_pointer)
void display_get_mouse_pos(int* x, int* y) {
    SDL_GetGlobalMouseState(x, y);
}

void display_set_position(DisplayContext* ctx, int x, int y) {
    if (ctx && ctx->window) {
        SDL_SetWindowPosition(ctx->window, x, y);
    }
}

void display_destroy(DisplayContext* ctx) {
    if (ctx) {
        if (ctx->window) SDL_DestroyWindow(ctx->window);
        free(ctx);
    }
    SDL_Quit();
}
