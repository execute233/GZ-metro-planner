#ifndef GZMP_MAP_RENDER_H
#define GZMP_MAP_RENDER_H
#include "../algo/router.h"
#include "../io/map_db.h"
typedef struct {
    uint32_t glyph;
    short color;
    unsigned char dim, continuation;
} MapCell;
typedef struct {
    int width, height;
    MapCell *cells;
} MapFrame;
typedef struct {
    double x, y, scale;
} Viewport;
int frame_resize(MapFrame *f, int width, int height);
void frame_clear(MapFrame *f);
void frame_dispose(MapFrame *f);
int utf8_next(const char **s, uint32_t *cp);
int unicode_width(uint32_t cp);
unsigned map_display_color(unsigned rgb);
void frame_text(MapFrame *f, int x, int y, int max_width, const char *text, int color, int dim);
void frame_glyph(MapFrame *f, int x, int y, uint32_t cp, int color, int dim);
void viewport_fit(Viewport *v, const MapDb *db, const Route *route, int cols, int rows);
void viewport_zoom(Viewport *v, double factor, int cols, int rows, double anchor_x,
                   double anchor_y);
/* Draw only in [0,cols) x [0,rows); returns count of unreadable tiles. */
int map_render(MapFrame *f, MapDb *db, Viewport v, const Route *route, int from, int to, int cols,
               int rows);
#endif
