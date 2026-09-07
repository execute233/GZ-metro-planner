#include "tui_state.h"
#include "tui_maintain.h"
#include "../io/metro_io.h"
#include "../render/utf8.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

int tui_search_station(const Metro *m, const char *query, int index, int *count) {
    int result = 0, n = 0;
    for (size_t i = 0; i < m->stations.rows.size; i++) {
        const Station *st = &m->stations.rows.items[i];
        bool prefix = true;
        for (size_t j = 0; query[j]; j++) {
            if (!st->pinyin_name[j] || tolower((unsigned char)query[j]) != tolower((unsigned char)st->pinyin_name[j])) { prefix = false; break; }
        }
        if (strstr(st->name, query) || prefix) { if (n == index) result = st->id; n++; }
    }
    if (count) *count = n;
    return result;
}
void tui_edit(char *text, size_t capacity, size_t *cursor, TuiEvent e) {
    if (e.type == EVENT_TEXT) { utf8_insert(text, capacity, cursor, e.text); return; }
    if (e.type != EVENT_KEY) return;
    const char *next;
    switch (e.key) {
        case TKEY_LEFT: *cursor = utf8_prev(text, *cursor); break;
        case TKEY_RIGHT:
            next = text + *cursor; utf8_decode(&next); *cursor = (size_t)(next-text); break;
        case TKEY_HOME: *cursor = 0; break;
        case TKEY_END: *cursor = strlen(text); break;
        case TKEY_BACKSPACE: utf8_backspace(text, cursor); break;
        case TKEY_DELETE:
            next = text + *cursor; utf8_decode(&next); memmove(text+*cursor, next, strlen(next)+1); break;
        default: break;
    }
}
int tui_state_init(TuiState *s, const char *dir) {
    *s = (TuiState){0}; s->data_dir = dir; s->running = true; s->dirty = true; s->view.zoom = s->view.fit_zoom = 1;
    route_init(&s->route);
    if (project_io_recover(dir, s->status, sizeof(s->status))) return -1;
    if (metro_io_load(dir, &s->metro)) { snprintf(s->status, sizeof(s->status), "拓扑数据载入失败：%s", dir); return -1; }
    if (graph_build(&s->graph, &s->metro)) { snprintf(s->status, sizeof(s->status), "图构建失败"); return -1; }
    s->layout_valid = map_io_load(dir, &s->metro, &s->map, s->status, sizeof(s->status)) == 0;
    if (s->layout_valid) snprintf(s->status, sizeof(s->status), "课程示例数据 | 未定位 %zu 站", s->metro.stations.rows.size-s->map.stations.size);
    return 0;
}
void tui_state_dispose(TuiState *s) {
    tui_form_dispose(&s->form); route_dispose(&s->route); graph_dispose(&s->graph); map_dispose(&s->map); project_dispose(&s->metro);
}
void tui_fit(TuiState *s, bool route) {
    MapPoint min, max;
    map_bounds(&s->map, route && s->has_route ? &s->route : NULL, &min, &max);
    viewport_fit(&s->view, min, max, !route);
}
void tui_state_resize(TuiState *s, int cols, int rows) {
    bool first = s->cols == 0 || s->view.cols < 1 || s->view.rows < 1;
    s->cols = cols; s->rows = rows;
    s->view.cols = cols >= 90 ? cols-37 : cols;
    s->view.rows = rows-3;
    if (first && cols >= 50 && rows >= 16) tui_fit(s, false);
    s->dirty = true;
}
int tui_commit(TuiState *s, Metro *metro, MapDocument *map) {
    if (metro_io_validate(metro, s->status, sizeof(s->status)) || map_io_validate(metro, map, s->status, sizeof(s->status))) return -1;
    Graph graph = {0};
    if (graph_build(&graph, metro)) { snprintf(s->status, sizeof(s->status), "图构建失败"); return -1; }
    if (project_io_save(s->data_dir, metro, map, s->status, sizeof(s->status))) { graph_dispose(&graph); return -1; }
    graph_dispose(&s->graph); project_dispose(&s->metro); map_dispose(&s->map);
    s->graph = graph; s->metro = *metro; s->map = *map;
    *metro = (Metro){0}; map_init(map);
    route_dispose(&s->route); route_init(&s->route); s->has_route = false; s->result_scroll = 0;
    if (!station_find_by_id(&s->metro.stations, s->from)) s->from = 0;
    if (!station_find_by_id(&s->metro.stations, s->to)) s->to = 0;
    if (!station_find_by_id(&s->metro.stations, s->selected)) s->selected = 0;
    snprintf(s->status, sizeof(s->status), "已保存 | 未定位 %zu 站", s->metro.stations.rows.size-s->map.stations.size);
    s->page = PAGE_MAP; s->focus = 0;
    return 0;
}
static void calculate(TuiState *s) {
    if (!s->from || !s->to) { snprintf(s->status, sizeof(s->status), "请确认起点和终点"); return; }
    s->has_route = router_find_route(&s->graph, &s->metro, s->from, s->to, s->metric, &s->route) == 0;
    s->result_scroll = 0;
    snprintf(s->status, sizeof(s->status), "%s", s->has_route ? "路线已更新；F 聚焦路线" : "两站不可达或查询失败");
    if (s->has_route) tui_fit(s, true);
}
void tui_state_event(TuiState *s, TuiEvent e) {
    if (e.type == EVENT_NONE) return;
    s->dirty = true;
    if (e.type == EVENT_QUIT) { s->running = false; return; }
    if (s->cols < 50 || s->rows < 16) {
        if (e.type == EVENT_TEXT && (e.text == 'q' || e.text == 'Q')) s->running = false;
        return;
    }
    if (s->page != PAGE_MAP) { tui_maintain_event(s, e); return; }
    if (e.type == EVENT_MOUSE && e.click && (s->cols >= 90 ? e.x > s->view.cols : s->focus != 0)) {
        int focus = e.y == 2 ? 1 : e.y == 3 ? 2 : e.y == 4 ? 3 : e.y == 5 ? 4 : e.y == 7 ? 5 : e.y == 8 ? 6 : e.y >= 9 ? 7 : 0;
        if (s->focus == 1 || s->focus == 2) {
            if (e.y >= 9 && e.y < s->rows-2) {
                s->candidate += e.y-9;
                tui_state_event(s, (TuiEvent){.type=EVENT_KEY,.key=TKEY_ENTER}); return;
            }
        }
        if (focus) {
            s->focus = focus;
            if (focus <= 2) { s->search[0]=0; s->cursor=0; s->candidate=0; }
            else if (focus <= 6) tui_state_event(s, (TuiEvent){.type=EVENT_KEY,.key=TKEY_ENTER});
        }
        return;
    }
    if (e.type == EVENT_KEY && e.key == TKEY_TAB) {
        s->focus = (s->focus+1)%8; s->search[0] = 0; s->cursor = 0; s->candidate = 0; return;
    }
    if (e.type == EVENT_KEY && e.key == TKEY_ESCAPE) { s->focus = 0; return; }
    if (s->focus == 1 || s->focus == 2) {
        int count; int id = tui_search_station(&s->metro, s->search, s->candidate, &count);
        if (e.type == EVENT_KEY && e.key == TKEY_DOWN) { if (s->candidate+1 < count) s->candidate++; }
        else if (e.type == EVENT_KEY && e.key == TKEY_UP) { if (s->candidate) s->candidate--; }
        else if (e.type == EVENT_KEY && e.key == TKEY_ENTER) {
            if (id) {
                if (s->focus == 1) { s->from = id; s->focus = 2; }
                else { s->to = id; s->focus = 3; }
                s->selected = id; s->search[0]=0; s->cursor=0; s->candidate=0;
            }
        } else { tui_edit(s->search, sizeof(s->search), &s->cursor, e); s->candidate = 0; }
        return;
    }
    if (e.type == EVENT_TEXT) {
        uint32_t ch = e.text < 128 ? (uint32_t)tolower((int)e.text) : e.text;
        if (ch == 'q') { s->running = false; return; }
        if (ch == 'm') { s->page = PAGE_MAINTAIN; return; }
        if (ch == 'f') { tui_fit(s, true); return; }
        if (ch == 'r') { tui_fit(s, false); return; }
        if (s->focus == 0) {
            if (ch == '+' || ch == '=') viewport_zoom_at(&s->view, 1.25, (MapPoint){s->view.cols, s->view.rows*2});
            if (ch == '-') viewport_zoom_at(&s->view, 0.8, (MapPoint){s->view.cols, s->view.rows*2});
            if (ch == 'w') s->view.center_y -= 8/s->view.zoom;
            if (ch == 's') s->view.center_y += 8/s->view.zoom;
            if (ch == 'a') s->view.center_x -= 8/s->view.zoom;
            if (ch == 'd') s->view.center_x += 8/s->view.zoom;
        }
    }
    if (e.type == EVENT_KEY) {
        if (s->focus == 0) {
            if (e.key == TKEY_UP) s->view.center_y -= 8/s->view.zoom;
            if (e.key == TKEY_DOWN) s->view.center_y += 8/s->view.zoom;
            if (e.key == TKEY_LEFT) s->view.center_x -= 8/s->view.zoom;
            if (e.key == TKEY_RIGHT) s->view.center_x += 8/s->view.zoom;
        } else if (s->focus == 3 && (e.key == TKEY_LEFT || e.key == TKEY_RIGHT || e.key == TKEY_ENTER)) s->metric = (s->metric+1)%3;
        else if (e.key == TKEY_ENTER) {
            if (s->focus == 4) calculate(s);
            if (s->focus == 5 && s->selected) s->from = s->selected;
            if (s->focus == 6 && s->selected) s->to = s->selected;
        }
        if (s->focus == 7) {
            if (e.key == TKEY_DOWN || e.key == TKEY_PAGEDOWN) s->result_scroll++;
            if ((e.key == TKEY_UP || e.key == TKEY_PAGEUP) && s->result_scroll) s->result_scroll--;
            int max = (int)s->route.edges_ids.size, prev = 0;
            for (size_t i = 0; i < s->route.edges_ids.size; i++) {
                const Edge *edge = edge_find_by_id(&s->metro.edges, s->route.edges_ids.items[i]);
                if (edge && edge->line_id != prev) { max++; prev = edge->line_id; }
            }
            max -= s->rows-13;
            if (max < 0) max = 0;
            if (s->result_scroll > max) s->result_scroll = max;
        }
    }
    if (e.type == EVENT_MOUSE && e.x >= 0 && e.x < s->view.cols && e.y >= 1 && e.y <= s->view.rows && (s->cols >= 90 || s->focus == 0)) {
        s->focus = 0;
        if (e.wheel) viewport_zoom_at(&s->view, e.wheel > 0 ? 1.25 : 0.8, (MapPoint){e.x*2, (e.y-1)*4});
        if (e.click) s->selected = map_hit_test(&s->map, &s->view, e.x, e.y-1);
    }
}
