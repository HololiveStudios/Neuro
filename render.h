#ifndef RENDER_H
#define RENDER_H

#include <SDL2/SDL.h>
#include <stdint.h>

typedef struct RenderContext RenderContext;

RenderContext* render_init_sdl2(SDL_Window* window);
void render_begin_frame(RenderContext* ctx);
void render_draw_rect(RenderContext* ctx, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void render_end_frame(RenderContext* ctx);
void render_destroy_sdl2(RenderContext* ctx);

#endif // RENDER_H
