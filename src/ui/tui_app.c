#include "tui_state.h"
#include "tui_maintain.h"
#include "../render/utf8.h"
#include "../render/render.h"
#include <stdio.h>
#include <string.h>

static const char *name(const TuiState *s, int id) {
    const Station *st = station_find_by_id(&s->metro.stations, id);
    return st ? st->name : "未选择";
}
static void side_text(CellSurface *out, int left, int y, const char *text, bool focus) {
    surface_text(out, left, y, out->cols-left-1, text, focus ? 0xebcb8b : 0xd8dee9);
}
static void edit_text(CellSurface *out, int x, int y, int width, const char *text, size_t cursor) {
    const char *start = text;
    int before = 0;
    for (const char *p = text; (size_t)(p-text) < cursor;) before += utf8_width(utf8_decode(&p));
    while (before >= width-1 && *start) before -= utf8_width(utf8_decode(&start));
    surface_text(out, x, y, width, start, 0xebcb8b);
    surface_put(out, x+before, y, '|', 0xffffff);
}
static void sidebar(TuiState *s, CellSurface *out) {
    int x = s->cols >= 90 ? s->view.cols+2 : 2;
    char text[256];
    side_text(out, x, 1, "路线规划", false);
    snprintf(text, sizeof(text), "%s 起点: %s", s->focus == 1 ? ">" : " ", name(s, s->from)); side_text(out, x, 2, text, s->focus == 1);
    snprintf(text, sizeof(text), "%s 终点: %s", s->focus == 2 ? ">" : " ", name(s, s->to)); side_text(out, x, 3, text, s->focus == 2);
    static const char *metrics[] = {"最少站点", "最短里程", "最少时间"};
    snprintf(text, sizeof(text), "%s 目标: %s", s->focus == 3 ? ">" : " ", metrics[s->metric]); side_text(out, x, 4, text, s->focus == 3);
    side_text(out, x, 5, "[规划路线]", s->focus == 4);
    if (s->focus == 1 || s->focus == 2) {
        side_text(out, x, 6, "搜索中文/拼音：", true);
        edit_text(out, x, 7, out->cols-x-1, s->search, s->cursor);
        for (int row = 9, i = s->candidate; row < out->rows-2; row++, i++) {
            int id = tui_search_station(&s->metro, s->search, i, NULL);
            if (!id) break;
            snprintf(text, sizeof(text), "%s %s", i == s->candidate ? ">" : " ", name(s, id)); side_text(out, x, row, text, i == s->candidate);
        }
        return;
    }
    snprintf(text, sizeof(text), "选中: %s", name(s, s->selected)); side_text(out, x, 6, text, false);
    side_text(out, x, 7, "[设为起点]", s->focus == 5); side_text(out, x, 8, "[设为终点]", s->focus == 6);
    side_text(out, x, 9, "行程结果（Tab 聚焦后 ↑↓ 滚动）", s->focus == 7);
    if (!s->has_route) return;
    snprintf(text, sizeof(text), "%d站 %.1fkm %.1f分钟", s->route.total_stations, s->route.total_meters/1000.0, s->route.total_seconds/60.0);
    side_text(out, x, 10, text, false);
    int row = 11, logical = 0, previous_line = 0;
    for (size_t i = 0; i < s->route.edges_ids.size; i++) {
        const Edge *e = edge_find_by_id(&s->metro.edges, s->route.edges_ids.items[i]);
        const Line *l = e ? line_find_by_id(&s->metro.lines, e->line_id) : NULL;
        if (!e) continue;
        if (e->line_id != previous_line) {
            snprintf(text, sizeof(text), "%s %s @ %s", previous_line ? "换乘" : "乘坐", l ? l->name : "?", name(s, s->route.stations.items[i]));
            if (logical++ >= s->result_scroll && row < out->rows-2) side_text(out, x, row++, text, false);
        }
        snprintf(text, sizeof(text), "%s → %s", name(s, s->route.stations.items[i]), name(s, s->route.stations.items[i+1]));
        if (logical++ >= s->result_scroll && row < out->rows-2) side_text(out, x, row++, text, false);
        previous_line = e->line_id;
    }
    if (s->route.edges_ids.size == 0) side_text(out, x, 11, "起点与终点相同", false);
}
int tui_state_render(TuiState *s, CellSurface *out, BrailleCanvas *b) {
    if (surface_resize(out, s->cols, s->rows)) return -1;
    surface_clear(out);
    if (s->cols < 50 || s->rows < 16) {
        surface_text(out, 0, 0, s->cols, "请调整终端至至少 50×16；Q 退出", 0xebcb8b); return 0;
    }
    surface_text(out, 1, 0, s->cols-2, "广州地铁 | 课程示例", 0x88c0d0);
    int legend_x=24;
    for (size_t i=0;i<s->metro.lines.rows.size;i++) {
        const Line *line=&s->metro.lines.rows.items[i];
        int width=render_display_width(line->name);
        if (legend_x+width>=s->view.cols) break;
        surface_text(out,legend_x,0,width,line->name,map_color(&s->map,line->id));
        legend_x+=width+2;
    }
    if (s->page == PAGE_MAP || s->page == PAGE_PICK) {
        if (s->cols >= 90 || s->focus == 0 || s->page == PAGE_PICK) {
            MapSelection sel = {s->selected, s->from, s->to, s->has_route ? &s->route : NULL};
            if (map_render(out, b, &s->view, &s->metro, &s->map, sel, NULL)) return -1;
        }
        if (s->cols >= 90) for (int y = 1; y < s->rows-2; y++) surface_put(out, s->view.cols, y, 0x2502, 0x4c566a);
        if (s->page == PAGE_PICK) {
            MapPoint p = viewport_project(&s->view, s->form.point);
            if (p.x >= 0 && p.y >= 0 && p.x < s->view.cols*2 && p.y < s->view.rows*4) surface_put(out, (int)(p.x/2), 1+(int)(p.y/4), '+', 0xffffff);
            surface_text(out, 1, s->rows-3, s->view.cols-2, "↑↓←→ 选位置 | 鼠标点击 | Enter 确认 | Esc 取消", 0xebcb8b);
        } else if (s->cols >= 90 || s->focus != 0) sidebar(s, out);
    } else {
        tui_maintain_render(s, out);
        if (s->page == PAGE_FORM) edit_text(out, 5, 7, out->cols-8, s->form.input, s->form.cursor);
    }
    surface_text(out, 0, s->rows-2, s->cols, s->status, 0xebcb8b);
    surface_text(out, 0, s->rows-1, s->cols, "Tab焦点 ↑↓←→移动 +/-缩放 F路线 R全图 M维护 Esc返回 Q退出", 0x88c0d0);
    return 0;
}
int tui_main_loop(const char *dir) {
    TuiState s; CellSurface out = {0}; BrailleCanvas b = {0};
    if (tui_state_init(&s, dir)) { fprintf(stderr, "%s\n", s.status); tui_state_dispose(&s); return -1; }
    if (tui_backend_init()) {
        fprintf(stderr, "需要交互式 Windows 终端；文本模式请使用 --text\n"); tui_state_dispose(&s); return -1;
    }
    int cols, rows, result = 0;
    tui_backend_size(&cols, &rows); tui_state_resize(&s, cols, rows);
    while (s.running) {
        if (s.dirty) {
            if (tui_state_render(&s, &out, &b)) { result = -1; break; }
            tui_backend_present(&out); s.dirty = false;
        }
        TuiEvent event = tui_backend_event();
        if (event.type == EVENT_RESIZE) { tui_backend_size(&cols, &rows); tui_state_resize(&s, cols, rows); }
        else tui_state_event(&s, event);
    }
    tui_backend_dispose(); surface_dispose(&out); braille_dispose(&b); tui_state_dispose(&s);
    return result;
}
