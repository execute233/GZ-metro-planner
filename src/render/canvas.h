#ifndef GZMP_CANVAS_H
#define GZMP_CANVAS_H
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint32_t cp, rgb; bool continuation; } Cell;
typedef struct { int cols, rows; Cell *cells; } CellSurface;
typedef struct { uint8_t mask; uint32_t rgb; int priority; } BrailleCell;
typedef struct { int cols, rows; BrailleCell *cells; } BrailleCanvas;
int surface_resize(CellSurface *s, int cols, int rows);
void surface_clear(CellSurface *s);
void surface_dispose(CellSurface *s);
void surface_put(CellSurface *s, int x, int y, uint32_t cp, uint32_t rgb);
void surface_text(CellSurface *s, int x, int y, int width, const char *text, uint32_t rgb);
int braille_resize(BrailleCanvas *b, int cols, int rows);
void braille_clear(BrailleCanvas *b);
void braille_dispose(BrailleCanvas *b);
void braille_pixel(BrailleCanvas *b, int x, int y, uint32_t rgb, int priority);
void braille_line(BrailleCanvas *b, double x0, double y0, double x1, double y1, uint32_t rgb, int priority);
void braille_present(const BrailleCanvas *b, CellSurface *s, int left, int top);
#endif
