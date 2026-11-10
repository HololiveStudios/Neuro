#include "gfx.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

uint32_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void canvas_init(Canvas *c, int w, int h)
{
    if (!c) return;
    c->w = w;
    c->h = h;
    c->px = (w > 0 && h > 0) ? (uint32_t *)calloc((size_t)w * (size_t)h, sizeof(uint32_t)) : NULL;
}

void canvas_free(Canvas *c)
{
    if (!c) return;
    free(c->px);
    c->px = NULL;
    c->w = 0;
    c->h = 0;
}

void canvas_fill(Canvas *c, uint32_t color)
{
    int i, total;
    if (!c || !c->px) return;
    total = c->w * c->h;
    for (i = 0; i < total; ++i) {
        c->px[i] = color;
    }
}

void canvas_rect(Canvas *c, int x, int y, int w, int h, uint32_t color)
{
    int x0, y0, x1, y1, row, col;
    if (!c || !c->px || w <= 0 || h <= 0) return;
    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x + w > c->w ? c->w : x + w;
    y1 = y + h > c->h ? c->h : y + h;
    if (x0 >= x1 || y0 >= y1) return;

    for (row = y0; row < y1; ++row) {
        uint32_t *line = c->px + row * c->w;
        for (col = x0; col < x1; ++col) {
            line[col] = color;
        }
    }
}

void canvas_disc(Canvas *c, int cx, int cy, int r, uint32_t color)
{
    int y0, y1, dy, r2;
    if (!c || !c->px || r <= 0) return;
    r2 = r * r;
    y0 = cy - r < 0 ? 0 : cy - r;
    y1 = cy + r >= c->h ? c->h - 1 : cy + r;

    for (dy = y0 - cy; dy <= y1 - cy; ++dy) {
        int dx_max = 0;
        int dyy = dy * dy;
        int x0, x1, col;
        uint32_t *line;
        if (dyy > r2) continue;
        while ((dx_max + 1) * (dx_max + 1) + dyy <= r2) {
            dx_max++;
        }
        x0 = cx - dx_max < 0 ? 0 : cx - dx_max;
        x1 = cx + dx_max >= c->w ? c->w - 1 : cx + dx_max;
        line = c->px + (cy + dy) * c->w;
        for (col = x0; col <= x1; ++col) {
            line[col] = color;
        }
    }
}

void canvas_border(Canvas *c, int x, int y, int w, int h, int thickness, uint32_t color)
{
    int t = thickness <= 0 ? 1 : thickness;
    if (!c || !c->px || w <= 0 || h <= 0) return;
    if (t * 2 >= w || t * 2 >= h) {
        canvas_rect(c, x, y, w, h, color);
        return;
    }
    canvas_rect(c, x, y, w, t, color);                     /* topo */
    canvas_rect(c, x, y + h - t, w, t, color);             /* base */
    canvas_rect(c, x, y + t, t, h - 2 * t, color);         /* esquerda */
    canvas_rect(c, x + w - t, y + t, t, h - 2 * t, color); /* direita */
}

/* Fonte bitmap básica 8x12 para caracteres ASCII 32 a 126 */
/* Cada caractere tem 12 bytes (1 byte por linha, 8 bits por coluna) */
static const uint8_t FONT_8X12[95][12] = {
    {0,0,0,0,0,0,0,0,0,0,0,0},             /* 32 ' ' */
    {0,0,0x18,0x18,0x18,0x18,0x18,0,0x18,0x18,0,0}, /* 33 '!' */
    {0,0x66,0x66,0x66,0,0,0,0,0,0,0,0},    /* 34 '"' */
    {0,0x66,0x66,0xff,0x66,0x66,0xff,0x66,0x66,0,0,0}, /* 35 '#' */
    {0,0x18,0x3e,0x60,0x3c,0x06,0x7c,0x18,0,0,0,0}, /* 36 '$' */
    {0,0x62,0x66,0x0c,0x18,0x30,0x66,0x46,0,0,0,0}, /* 37 '%' */
    {0,0x38,0x6c,0x38,0x76,0xdc,0xcc,0x76,0,0,0,0}, /* 38 '&' */
    {0,0x18,0x18,0x30,0,0,0,0,0,0,0,0},    /* 39 ''' */
    {0,0x0c,0x18,0x30,0x30,0x30,0x18,0x0c,0,0,0,0}, /* 40 '(' */
    {0,0x30,0x18,0x0c,0x0c,0x0c,0x18,0x30,0,0,0,0}, /* 41 ')' */
    {0,0,0x66,0x3c,0xff,0x3c,0x66,0,0,0,0,0}, /* 42 '*' */
    {0,0,0x18,0x18,0x7e,0x18,0x18,0,0,0,0,0}, /* 43 '+' */
    {0,0,0,0,0,0,0,0x18,0x18,0x30,0,0},    /* 44 ',' */
    {0,0,0,0,0x7e,0,0,0,0,0,0,0},          /* 45 '-' */
    {0,0,0,0,0,0,0,0x18,0x18,0,0,0},       /* 46 '.' */
    {0,0x02,0x06,0x0c,0x18,0x30,0x60,0x40,0,0,0,0}, /* 47 '/' */
    {0,0x3c,0x66,0x6e,0x76,0x66,0x66,0x3c,0,0,0,0}, /* 48 '0' */
    {0,0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0,0,0,0}, /* 49 '1' */
    {0,0x3c,0x66,0x06,0x1c,0x30,0x60,0x7e,0,0,0,0}, /* 50 '2' */
    {0,0x3c,0x66,0x06,0x1c,0x06,0x66,0x3c,0,0,0,0}, /* 51 '3' */
    {0,0x0c,0x1c,0x3c,0x6c,0x7e,0x0c,0x0c,0,0,0,0}, /* 52 '4' */
    {0,0x7e,0x60,0x7c,0x06,0x06,0x66,0x3c,0,0,0,0}, /* 53 '5' */
    {0,0x1c,0x30,0x60,0x7c,0x66,0x66,0x3c,0,0,0,0}, /* 54 '6' */
    {0,0x7e,0x06,0x0c,0x18,0x30,0x30,0x30,0,0,0,0}, /* 55 '7' */
    {0,0x3c,0x66,0x66,0x3c,0x66,0x66,0x3c,0,0,0,0}, /* 56 '8' */
    {0,0x3c,0x66,0x66,0x3e,0x06,0x0c,0x38,0,0,0,0}, /* 57 '9' */
    {0,0,0x18,0x18,0,0,0x18,0x18,0,0,0,0}, /* 58 ':' */
    {0,0,0x18,0x18,0,0,0x18,0x18,0x30,0,0,0}, /* 59 ';' */
    {0,0x0c,0x18,0x30,0x60,0x30,0x18,0x0c,0,0,0,0}, /* 60 '<' */
    {0,0,0,0x7e,0,0x7e,0,0,0,0,0,0},       /* 61 '=' */
    {0,0x30,0x18,0x0c,0x06,0x0c,0x18,0x30,0,0,0,0}, /* 62 '>' */
    {0,0x3c,0x66,0x0c,0x18,0x18,0,0x18,0,0,0,0},    /* 63 '?' */
    {0,0x3c,0x66,0x6e,0x6e,0x60,0x62,0x3c,0,0,0,0}, /* 64 '@' */
    {0,0x18,0x3c,0x66,0x7e,0x66,0x66,0x66,0,0,0,0}, /* 65 'A' */
    {0,0x7c,0x66,0x66,0x7c,0x66,0x66,0x7c,0,0,0,0}, /* 66 'B' */
    {0,0x3c,0x66,0x60,0x60,0x60,0x66,0x3c,0,0,0,0}, /* 67 'C' */
    {0,0x78,0x6c,0x66,0x66,0x66,0x6c,0x78,0,0,0,0}, /* 68 'D' */
    {0,0x7e,0x60,0x60,0x7c,0x60,0x60,0x7e,0,0,0,0}, /* 69 'E' */
    {0,0x7e,0x60,0x60,0x7c,0x60,0x60,0x60,0,0,0,0}, /* 70 'F' */
    {0,0x3c,0x66,0x60,0x6e,0x66,0x66,0x3e,0,0,0,0}, /* 71 'G' */
    {0,0x66,0x66,0x66,0x7e,0x66,0x66,0x66,0,0,0,0}, /* 72 'H' */
    {0,0x3c,0x18,0x18,0x18,0x18,0x18,0x3c,0,0,0,0}, /* 73 'I' */
    {0,0x0e,0x06,0x06,0x06,0x66,0x66,0x3c,0,0,0,0}, /* 74 'J' */
    {0,0x66,0x6c,0x78,0x70,0x78,0x6c,0x66,0,0,0,0}, /* 75 'K' */
    {0,0x60,0x60,0x60,0x60,0x60,0x60,0x7e,0,0,0,0}, /* 76 'L' */
    {0,0x63,0x77,0x7f,0x6b,0x63,0x63,0x63,0,0,0,0}, /* 77 'M' */
    {0,0x66,0x76,0x7e,0x7e,0x6e,0x66,0x66,0,0,0,0}, /* 78 'N' */
    {0,0x3c,0x66,0x66,0x66,0x66,0x66,0x3c,0,0,0,0}, /* 79 'O' */
    {0,0x7c,0x66,0x66,0x7c,0x60,0x60,0x60,0,0,0,0}, /* 80 'P' */
    {0,0x3c,0x66,0x66,0x66,0x66,0x6c,0x36,0,0,0,0}, /* 81 'Q' */
    {0,0x7c,0x66,0x66,0x7c,0x78,0x6c,0x66,0,0,0,0}, /* 82 'R' */
    {0,0x3c,0x66,0x60,0x3c,0x06,0x66,0x3c,0,0,0,0}, /* 83 'S' */
    {0,0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0,0,0,0}, /* 84 'T' */
    {0,0x66,0x66,0x66,0x66,0x66,0x66,0x3c,0,0,0,0}, /* 85 'U' */
    {0,0x66,0x66,0x66,0x66,0x66,0x3c,0x18,0,0,0,0}, /* 86 'V' */
    {0,0x63,0x63,0x63,0x6b,0x7f,0x77,0x63,0,0,0,0}, /* 87 'W' */
    {0,0x66,0x66,0x3c,0x18,0x3c,0x66,0x66,0,0,0,0}, /* 88 'X' */
    {0,0x66,0x66,0x66,0x3c,0x18,0x18,0x18,0,0,0,0}, /* 89 'Y' */
    {0,0x7e,0x06,0x0c,0x18,0x30,0x60,0x7e,0,0,0,0}, /* 90 'Z' */
    {0,0x3c,0x30,0x30,0x30,0x30,0x30,0x3c,0,0,0,0}, /* 91 '[' */
    {0,0x40,0x60,0x30,0x18,0x0c,0x06,0x02,0,0,0,0}, /* 92 '\' */
    {0,0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0,0,0,0}, /* 93 ']' */
    {0,0x18,0x3c,0x66,0,0,0,0,0,0,0,0},    /* 94 '^' */
    {0,0,0,0,0,0,0,0,0,0xff,0,0},          /* 95 '_' */
    {0,0x30,0x18,0x0c,0,0,0,0,0,0,0,0},    /* 96 '`' */
    {0,0,0x3c,0x06,0x3e,0x66,0x66,0x3e,0,0,0,0}, /* 97 'a' */
    {0,0x60,0x60,0x7c,0x66,0x66,0x66,0x7c,0,0,0,0}, /* 98 'b' */
    {0,0,0x3c,0x66,0x60,0x60,0x66,0x3c,0,0,0,0}, /* 99 'c' */
    {0,0x06,0x06,0x3e,0x66,0x66,0x66,0x3e,0,0,0,0}, /* 100 'd' */
    {0,0,0x3c,0x66,0x7e,0x60,0x66,0x3c,0,0,0,0}, /* 101 'e' */
    {0,0x1c,0x30,0x7c,0x30,0x30,0x30,0x30,0,0,0,0}, /* 102 'f' */
    {0,0,0x3e,0x66,0x66,0x3e,0x06,0x3c,0,0,0,0}, /* 103 'g' */
    {0,0x60,0x60,0x7c,0x66,0x66,0x66,0x66,0,0,0,0}, /* 104 'h' */
    {0,0x18,0,0x38,0x18,0x18,0x18,0x3c,0,0,0,0}, /* 105 'i' */
    {0,0x06,0,0x06,0x06,0x06,0x66,0x3c,0,0,0,0}, /* 106 'j' */
    {0,0x60,0x60,0x66,0x6c,0x78,0x6c,0x66,0,0,0,0}, /* 107 'k' */
    {0,0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0,0,0,0}, /* 108 'l' */
    {0,0,0x66,0x7f,0x7f,0x6b,0x63,0x63,0,0,0,0}, /* 109 'm' */
    {0,0,0x7c,0x66,0x66,0x66,0x66,0x66,0,0,0,0}, /* 110 'n' */
    {0,0,0x3c,0x66,0x66,0x66,0x66,0x3c,0,0,0,0}, /* 111 'o' */
    {0,0,0x7c,0x66,0x66,0x7c,0x60,0x60,0,0,0,0}, /* 112 'p' */
    {0,0,0x3e,0x66,0x66,0x3e,0x06,0x06,0,0,0,0}, /* 113 'q' */
    {0,0,0x7c,0x66,0x60,0x60,0x60,0x60,0,0,0,0}, /* 114 'r' */
    {0,0,0x3e,0x60,0x3c,0x06,0x66,0x3c,0,0,0,0}, /* 115 's' */
    {0,0x30,0x7c,0x30,0x30,0x30,0x34,0x18,0,0,0,0}, /* 116 't' */
    {0,0,0x66,0x66,0x66,0x66,0x66,0x3e,0,0,0,0}, /* 117 'u' */
    {0,0,0x66,0x66,0x66,0x66,0x3c,0x18,0,0,0,0}, /* 118 'v' */
    {0,0,0x63,0x63,0x6b,0x7f,0x77,0x63,0,0,0,0}, /* 119 'w' */
    {0,0,0x66,0x3c,0x18,0x3c,0x66,0x66,0,0,0,0}, /* 120 'x' */
    {0,0,0x66,0x66,0x66,0x3e,0x06,0x3c,0,0,0,0}, /* 121 'y' */
    {0,0,0x7e,0x0c,0x18,0x30,0x60,0x7e,0,0,0,0}, /* 122 'z' */
    {0,0x0e,0x18,0x18,0x70,0x18,0x18,0x0e,0,0,0,0}, /* 123 '{' */
    {0,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0,0,0,0}, /* 124 '|' */
    {0,0x70,0x18,0x18,0x0e,0x18,0x18,0x70,0,0,0,0}, /* 125 '}' */
    {0,0x36,0x5c,0,0,0,0,0,0,0,0,0}         /* 126 '~' */
};

/* Decodifica o próximo caractere UTF-8 ou byte simples */
static uint32_t next_utf8(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t c = *s;
    if (!c) return 0;
    if (c < 0x80) {
        (*p)++;
        return c;
    } else if ((c & 0xe0) == 0xc0 && s[1]) {
        uint32_t cp = ((c & 0x1f) << 6) | (s[1] & 0x3f);
        *p += 2;
        return cp;
    } else if ((c & 0xf0) == 0xe0 && s[1] && s[2]) {
        uint32_t cp = ((c & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f);
        *p += 3;
        return cp;
    } else {
        (*p)++;
        return c;
    }
}

/* Converte codepoint Unicode para caractere base ASCII aproximado e acento */
static char normalize_char(uint32_t cp)
{
    switch (cp) {
    case 0x00c1: case 0x00c0: case 0x00c2: case 0x00c3: case 0x00c4: return 'A';
    case 0x00e1: case 0x00e0: case 0x00e2: case 0x00e3: case 0x00e4: return 'a';
    case 0x00c9: case 0x00c8: case 0x00ca: case 0x00cb:             return 'E';
    case 0x00e9: case 0x00e8: case 0x00ea: case 0x00eb:             return 'e';
    case 0x00cd: case 0x00cc: case 0x00ce: case 0x00cf:             return 'I';
    case 0x00ed: case 0x00ec: case 0x00ee: case 0x00ef:             return 'i';
    case 0x00d3: case 0x00d2: case 0x00d4: case 0x00d5: case 0x00d6: return 'O';
    case 0x00f3: case 0x00f2: case 0x00f4: case 0x00f5: case 0x00f6: return 'o';
    case 0x00da: case 0x00d9: case 0x00db: case 0x00dc:             return 'U';
    case 0x00fa: case 0x00f9: case 0x00fb: case 0x00fc:             return 'u';
    case 0x00c7: return 'C';
    case 0x00e7: return 'c';
    default:
        return (cp >= 32 && cp <= 126) ? (char)cp : ' ';
    }
}

int gfx_text_width(const char *text, int scale)
{
    int char_w = scale == 1 ? 16 : 8;
    int len = 0;
    const char *p = text;
    if (!text) return 0;
    while (*p) {
        next_utf8(&p);
        len++;
    }
    return len * char_w;
}

void canvas_text(Canvas *c, int x, int y, const char *text, uint32_t color, int scale)
{
    const char *p = text;
    int cur_x = x;
    int mult = scale == 1 ? 2 : 1;
    int char_w = scale == 1 ? 16 : 8;

    if (!c || !c->px || !text) return;

    while (*p) {
        uint32_t cp = next_utf8(&p);
        char ch = normalize_char(cp);
        if (ch >= 32 && ch <= 126) {
            const uint8_t *glyph = FONT_8X12[ch - 32];
            int r, b;
            for (r = 0; r < 12; ++r) {
                uint8_t bits = glyph[r];
                for (b = 0; b < 8; ++b) {
                    if (bits & (0x80 >> b)) {
                        int px_x = cur_x + b * mult;
                        int px_y = y + r * mult;
                        if (scale == 1) {
                            canvas_rect(c, px_x, px_y, 2, 2, color);
                        } else {
                            if (px_x >= 0 && px_x < c->w && px_y >= 0 && px_y < c->h) {
                                c->px[px_y * c->w + px_x] = color;
                            }
                        }
                    }
                }
            }
        }
        cur_x += char_w;
    }
}

int gfx_wrap(const char *text, int maxw, int scale, char out[][64], int max_lines)
{
    int char_w = scale == 1 ? 16 : 8;
    int max_chars_by_width = maxw / (char_w > 0 ? char_w : 8);
    int line_cap = max_chars_by_width < 63 ? max_chars_by_width : 63;
    int num_lines = 0;
    const char *p = text;

    if (!text || max_lines <= 0) return 0;
    if (line_cap < 1) line_cap = 1;

    while (*p && num_lines < max_lines) {
        const char *line_start;
        const char *word_end;
        const char *next_scan;
        int line_chars = 0;
        int copy_len = 0;

        /* Pula espaços iniciais da linha */
        while (*p == ' ' || *p == '\t' || *p == '\n') {
            if (*p == '\n') {
                /* Quebra forçada de linha */
                p++;
                out[num_lines][0] = '\0';
                num_lines++;
                if (num_lines >= max_lines) return num_lines;
            } else {
                p++;
            }
        }
        if (!*p) break;

        line_start = p;
        word_end = p;
        next_scan = p;

        while (*next_scan && *next_scan != '\n') {
            const char *saved_ptr = next_scan;
            next_utf8(&next_scan);
            line_chars++;

            if (*saved_ptr == ' ' || *saved_ptr == '\t') {
                word_end = saved_ptr;
            }

            if (line_chars >= line_cap) {
                if (word_end > line_start) {
                    /* Quebra na última palavra */
                    copy_len = (int)(word_end - line_start);
                    p = word_end;
                    while (*p == ' ' || *p == '\t') p++;
                } else {
                    /* Palavra única longa forçada */
                    copy_len = (int)(next_scan - line_start);
                    p = next_scan;
                }
                break;
            }
        }

        if (copy_len == 0) {
            copy_len = (int)(next_scan - line_start);
            p = next_scan;
            if (*p == '\n') p++;
        }

        if (copy_len > 63) copy_len = 63;
        memcpy(out[num_lines], line_start, (size_t)copy_len);
        out[num_lines][copy_len] = '\0';
        num_lines++;
    }

    return num_lines;
}
