#ifndef GZMP_MAP_LAYOUT_H
#define GZMP_MAP_LAYOUT_H
#include "arraylist.h"
#include <stdint.h>
#include <stdbool.h>
typedef struct { double x, y; } MapPoint;
typedef enum { MAP_SPACE_SCHEMATIC, MAP_SPACE_GEOGRAPHIC } MapCoordinateSpace;
typedef struct { int station_id; MapPoint point; int label_dx, label_dy; bool label_left; } MapStation;
typedef struct { int edge_id, order; MapPoint point; } MapVertex;
typedef struct { int line_id; uint32_t rgb; } MapStyle;
DEFINE_ARRAYLIST(MapStation, MapStation, map_station)
DEFINE_ARRAYLIST(MapVertex, MapVertex, map_vertex)
DEFINE_ARRAYLIST(MapStyle, MapStyle, map_style)
typedef struct {
    MapCoordinateSpace space;
    ArrayList_MapStation stations;
    ArrayList_MapVertex vertices;
    ArrayList_MapStyle styles;
} MapDocument;
void map_init(MapDocument *map);
void map_dispose(MapDocument *map);
const MapStation *map_station(const MapDocument *map, int id);
uint32_t map_color(const MapDocument *map, int line_id);
int map_clone(MapDocument *out, const MapDocument *source);
#endif
