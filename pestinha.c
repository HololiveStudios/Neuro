#include "display.h"
#include "render.h"
#include "ui.h"
#include "fx.h"
#include <stdbool.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    DisplayContext* display = display_init("Pestinha C", 64, 64);
    if (!display) return 1;

    RenderContext* render = render_init_sdl2(display->window);
    if (!render) {
        display_destroy(display);
        return 1;
    }

    fx_init();

    bool running = true;
    int pet_x = 500, pet_y = 300;

    while (running) {
        Uint32 frame_start = SDL_GetTicks();

        // 1. Processa eventos e atualização do Pet
        display_poll_events(display, &running);
        display_get_mouse_pos(&mouse_x, &mouse_y);
        display_set_position(display, pet_x, pet_y);

        // 2. Renderização
        render_begin_frame(render);
        render_draw_rect(render, 0, 0, 64, 64, 50, 150, 240, 255);
        fx_update_and_draw(render);
        render_end_frame(render);

        // 3. Controle rígido de FPS (~60 FPS = ~16ms por quadro)
        Uint32 frame_time = SDL_GetTicks() - frame_start;
        if (frame_time < 16) {
            SDL_Delay(16 - frame_time);
        }
    }

    render_destroy_sdl2(render);
    display_destroy(display);

    return 0;
}
