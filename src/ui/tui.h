#ifndef GZMP_TUI_H
#define GZMP_TUI_H
#include "../algo/graph.h"
#include "../render/map_render.h"
#include "maintenance.h"
typedef struct {
    MapDb *map;
    Graph graph;
    Route route;
    Viewport view;
    int from, to, focus, metric, candidate, scroll, ready;
    int matches[MAP_LIMIT], match_count;
    char query[128], status[256];
    Maintenance edit;
} TuiState;
int tui_init(TuiState *s, MapDb *map, int width, int height);
void tui_dispose(TuiState *s);
void tui_search(TuiState *s);
void tui_plan(TuiState *s, int width, int height);
void tui_frame(TuiState *s, MapFrame *frame);
/* Submit a maintenance field; 1 means data was saved and the map refreshed. */
int tui_maintenance_submit(TuiState *s, const char *path, int width, int height);
int tui_run(const char *path);
int tui_snapshot(const char *path, const char *output, int width, int height, const char *from,
                 const char *to);
#endif
