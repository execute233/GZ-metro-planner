#include "canvas.h"
#include "utf8.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

static bool valid_size(int cols, int rows) {
    return cols > 0 && rows > 0 && cols <= 4096 && rows <= 4096;
}
int surface_resize(CellSurface *s, int cols, int rows) {
    if (!valid_size(cols, rows)) return -1;
    if (s->cols == cols && s->rows == rows && s->cells) return 0;
    Cell *cells = calloc((size_t)cols * rows, sizeof(*cells));
    if (!cells) return -1;
    free(s->cells); *s = (CellSurface){cols, rows, cells};
    return 0;
}
void surface_clear(CellSurface *s) {
    for (int i = 0; i < s->cols * s->rows; i++) s->cells[i] = (Cell){' ', 0xd8dee9, false};
}
void surface_dispose(CellSurface *s) { free(s->cells); *s = (CellSurface){0}; }
static void erase_cell(CellSurface *s, int x, int y) {
    Cell *c = &s->cells[y * s->cols + x];
    if (c->continuation && x) s->cells[y * s->cols + x - 1] = (Cell){' ', c->rgb, false};
    if (utf8_width(c->cp) == 2 && x + 1 < s->cols) s->cells[y * s->cols + x + 1] = (Cell){' ', c->rgb, false};
    *c = (Cell){' ', c->rgb, false};
}
void surface_put(CellSurface *s, int x, int y, uint32_t cp, uint32_t rgb) {
    int width = utf8_width(cp);
    if (x < 0 || y < 0 || y >= s->rows || x + width > s->cols || width == 0) return;
    erase_cell(s, x, y);
    if (width == 2) erase_cell(s, x + 1, y);
    s->cells[y * s->cols + x] = (Cell){cp, rgb, false};
    if (width == 2) s->cells[y * s->cols + x + 1] = (Cell){0, rgb, true};
}
void surface_text(CellSurface *s, int x, int y, int width, const char *text, uint32_t rgb) {
    int used = 0;
    while (*text) {
        uint32_t cp = utf8_decode(&text);
        int w = utf8_width(cp);
        if (used + w > width) break;
        if (w) surface_put(s, x + used, y, cp, rgb);
        used += w;
    }
}
int braille_resize(BrailleCanvas *b, int cols, int rows) {
    if (!valid_size(cols, rows)) return -1;
    if (b->cols == cols && b->rows == rows && b->cells) return 0;
    BrailleCell *cells = calloc((size_t)cols * rows, sizeof(*cells));
    if (!cells) return -1;
    free(b->cells); *b = (BrailleCanvas){cols, rows, cells};
    return 0;
}
void braille_clear(BrailleCanvas *b) {
    for (int i = 0; i < b->cols * b->rows; i++) b->cells[i] = (BrailleCell){0, 0, INT_MIN};
}
void braille_dispose(BrailleCanvas *b) { free(b->cells); *b = (BrailleCanvas){0}; }
void braille_pixel(BrailleCanvas *b, int x, int y, uint32_t rgb, int priority) {
    static const uint8_t bits[4][2] = {{1, 8}, {2, 16}, {4, 32}, {64, 128}};
    if (x < 0 || y < 0 || x >= b->cols * 2 || y >= b->rows * 4) return;
    BrailleCell *c = &b->cells[(y / 4) * b->cols + x / 2];
    c->mask |= bits[y % 4][x % 2];
    if (priority >= c->priority) { c->rgb = rgb; c->priority = priority; }
}
/* Liang-Barsky clips doubles before integer conversion or rasterization. */
static bool clip_test(double p, double q, double *lo, double *hi) {
    if (p == 0) return q >= 0;
    double r = q / p;
    if (p < 0) { if (r > *hi) return false; if (r > *lo) *lo = r; }
    else { if (r < *lo) return false; if (r < *hi) *hi = r; }
    return true;
}
void braille_line(BrailleCanvas *b, double x0, double y0, double x1, double y1, uint32_t rgb, int priority) {
    if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1)) return;
    double dx = x1-x0, dy = y1-y0, lo = 0, hi = 1;
    if (!isfinite(dx) || !isfinite(dy) || !clip_test(-dx, x0, &lo, &hi) ||
        !clip_test(dx, b->cols*2-1-x0, &lo, &hi) || !clip_test(-dy, y0, &lo, &hi) ||
        !clip_test(dy, b->rows*4-1-y0, &lo, &hi)) return;
    int ax = (int)lround(x0 + lo*dx), ay = (int)lround(y0 + lo*dy);
    int bx = (int)lround(x0 + hi*dx), by = (int)lround(y0 + hi*dy);
    int ix = abs(bx-ax), iy = -abs(by-ay), sx = ax < bx ? 1 : -1, sy = ay < by ? 1 : -1;
    int err = ix + iy;
    for (;;) {
        braille_pixel(b, ax, ay, rgb, priority);
        if (ax == bx && ay == by) break;
        int e2 = 2*err;
        if (e2 >= iy) { err += iy; ax += sx; }
        if (e2 <= ix) { err += ix; ay += sy; }
    }
}
void braille_present(const BrailleCanvas *b, CellSurface *s, int left, int top) {
    for (int y = 0; y < b->rows; y++) for (int x = 0; x < b->cols; x++) {
        const BrailleCell *c = &b->cells[y*b->cols+x];
        if (c->mask) surface_put(s, left+x, top+y, 0x2800+c->mask, c->rgb);
    }
}
