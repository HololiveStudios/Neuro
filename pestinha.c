#include "display.h"
#include "fx.h"
#include "render.h"

#include <stdbool.h>
#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    const int width = 320;
    const int height = 200;
    DisplayContext *display = display_init("Pestinha C", width, height);
    if (!display) {
        return 1;
    }

    RenderContext *render = render_init_sdl2(display->window);
    if (!render) {
        display_destroy(display);
        return 1;
    }

    Fx *fx = fx_open();
    if (!fx) {
        render_destroy_sdl2(render);
        display_destroy(display);
        fprintf(stderr, "[PESTINHA] Erro ao alocar efeitos visuais\n");
        return 1;
    }

    bool running = true;
    while (running) {
        display_poll_events(display, &running);
        fx_update(fx, 1.0 / 60.0, NULL, NULL, 0);

        render_begin_frame(render);
        fx_draw(fx, render);
        render_end_frame(render);
        SDL_Delay(16);
    }

    fx_close(fx);
    render_destroy_sdl2(render);
    display_destroy(display);
    return 0;
}
