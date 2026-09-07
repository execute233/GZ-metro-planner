#ifndef GZMP_MAP_RENDERER_H
#define GZMP_MAP_RENDERER_H
#include "canvas.h"
#include "viewport.h"
#include "../algo/router.h"
typedef struct {
    void *context;
    MapCoordinateSpace space;
    void (*draw)(void *context, CellSurface *surface, const Viewport *view);
} BackgroundLayer;
typedef struct { int selected, from, to; const Route *route; } MapSelection;
void map_bounds(const MapDocument *map, const Route *route, MapPoint *min, MapPoint *max);
int map_hit_test(const MapDocument *map, const Viewport *v, int col, int row);
int map_render(CellSurface *surface, BrailleCanvas *canvas, const Viewport *v,
    const Metro *metro, const MapDocument *map, MapSelection selection, const BackgroundLayer *background);
#endif
