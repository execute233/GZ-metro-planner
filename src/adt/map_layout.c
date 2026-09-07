#include "map_layout.h"
void map_init(MapDocument *map) { *map = (MapDocument){0}; }
void map_dispose(MapDocument *map) {
    al_map_station_dispose(&map->stations); al_map_vertex_dispose(&map->vertices); al_map_style_dispose(&map->styles);
}
const MapStation *map_station(const MapDocument *map, int id) {
    for (size_t i = 0; i < map->stations.size; i++) if (map->stations.items[i].station_id == id) return &map->stations.items[i];
    return NULL;
}
uint32_t map_color(const MapDocument *map, int id) {
    for (size_t i = 0; i < map->styles.size; i++) if (map->styles.items[i].line_id == id) return map->styles.items[i].rgb;
    return 0x88c0d0;
}
int map_clone(MapDocument *out, const MapDocument *source) {
    map_init(out); out->space = source->space;
    for (size_t i = 0; i < source->stations.size; i++) if (al_map_station_push(&out->stations, source->stations.items[i])) goto fail;
    for (size_t i = 0; i < source->vertices.size; i++) if (al_map_vertex_push(&out->vertices, source->vertices.items[i])) goto fail;
    for (size_t i = 0; i < source->styles.size; i++) if (al_map_style_push(&out->styles, source->styles.items[i])) goto fail;
    return 0;
fail: map_dispose(out); return -1;
}
