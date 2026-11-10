#include "ui.h"
#include "render.h"
#include <stdio.h>

void ui_draw_box(RenderContext* render, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    render_draw_rect(render, x, y, w, h, r, g, b, 220);
    render_draw_rect(render, x, y, w, 2, 30, 30, 30, 255);
    render_draw_rect(render, x, y + h - 2, w, 2, 30, 30, 30, 255);
    render_draw_rect(render, x, 2, y, h, 30, 30, 30, 255);
    render_draw_rect(render, x + w - 2, y, 2, h, 30, 30, 30, 255);
}

void ui_draw_punishment_corner(RenderContext* render, int x, int y) {
    ui_draw_box(render, x, y, 120, 40, 240, 200, 50);
}
