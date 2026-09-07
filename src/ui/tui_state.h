#ifndef GZMP_TUI_STATE_H
#define GZMP_TUI_STATE_H
#include "tui_backend.h"
#include "../render/map_renderer.h"
#include "../io/project_io.h"
typedef enum { PAGE_MAP, PAGE_MAINTAIN, PAGE_FORM, PAGE_PICK, PAGE_CONFIRM } TuiPage;
typedef enum { MAINT_ADD_STATION, MAINT_DELETE_STATION, MAINT_ADD_LINE, MAINT_DELETE_LINE } MaintainAction;
typedef struct {
    MaintainAction action;
    int step, candidate, target_id;
    char input[128]; size_t cursor;
    Station station;
    Line line;
    uint32_t rgb;
    ArrayList_Edge edges;
    MapPoint point;
} MaintainForm;
typedef struct {
    Metro metro; Graph graph; MapDocument map;
    TuiPage page; int focus, menu;
    Viewport view;
    int cols, rows, selected, from, to, candidate, result_scroll;
    char search[128]; size_t cursor;
    RouteMetric metric; Route route;
    bool running, has_route, layout_valid, dirty;
    char status[256];
    MaintainForm form;
    const char *data_dir;
} TuiState;
int tui_state_init(TuiState *s, const char *dir);
void tui_state_dispose(TuiState *s);
void tui_state_resize(TuiState *s, int cols, int rows);
void tui_state_event(TuiState *s, TuiEvent event);
int tui_state_render(TuiState *s, CellSurface *surface, BrailleCanvas *canvas);
int tui_search_station(const Metro *m, const char *query, int index, int *count);
void tui_edit(char *text, size_t capacity, size_t *cursor, TuiEvent event);
void tui_fit(TuiState *s, bool route);
int tui_commit(TuiState *s, Metro *metro, MapDocument *map);
int tui_main_loop(const char *dir);
#endif
