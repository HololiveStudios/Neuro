#ifndef DISPLAY_H
#define DISPLAY_H

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef struct {
    SDL_Window* window;
    int screen_width;
    int screen_height;
} DisplayContext;

DisplayContext* display_init(const char* title, int width, int height);
void display_poll_events(DisplayContext* ctx, bool* running);
void display_get_mouse_pos(int* x, int* y);
void display_set_position(DisplayContext* ctx, int x, int y);
void display_destroy(DisplayContext* ctx);

#endif // DISPLAY_H
