#include "render.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>

struct RenderContext {
    SDL_Renderer* renderer;
};

RenderContext* render_init_sdl2(SDL_Window* window) {
    RenderContext* ctx = (RenderContext*)malloc(sizeof(RenderContext));
    if (!ctx) return NULL;

    ctx->renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!ctx->renderer) {
        ctx->renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }

    if (!ctx->renderer) {
        fprintf(stderr, "[RENDER] Erro ao criar SDL_Renderer: %s
", SDL_GetError());
        free(ctx);
        return NULL;
    }

    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    return ctx;
}

void render_begin_frame(RenderContext* ctx) {
    // Define a cor de limpeza como 100% transparente (Alpha = 0)
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 0);
    SDL_RenderClear(ctx->renderer);
}

void render_draw_rect(RenderContext* ctx, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_Rect rect = { x, y, w, h };
    SDL_SetRenderDrawColor(ctx->renderer, r, g, b, a);
    SDL_RenderFillRect(ctx->renderer, &rect);
}

void render_end_frame(RenderContext* ctx) {
    SDL_RenderPresent(ctx->renderer);
}

void render_destroy_sdl2(RenderContext* ctx) {
    if (ctx) {
        if (ctx->renderer) SDL_DestroyRenderer(ctx->renderer);
        free(ctx);
    }
}
